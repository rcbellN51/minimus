#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "player.h"
#include "pls.h"
#include "state.h"

#define DEFAULT_PLAYLIST "/usr/local/share/minimus/stations.pls"
#define MINIMUS_VERSION     "0.1"
#define INPUT_SIZE          1024
#define COMMAND_SIZE        2048
#define OUTPUT_SIZE         2048
#define TRACK_SIZE          2048
#define MESSAGE_SIZE        128
#define STATUS_LABEL_WIDTH  9
#define HEADER_ROWS         3

/*
 * MINIX color palette, shared with mininet.
 * ANSI 256-color escape sequences.
 */
#define COLOR_RESET         "\033[0m"
#define COLOR_YELLOW        "\033[38;5;221m"
#define COLOR_WHITE_BLUE    "\033[38;5;15;48;5;24m"
#define COLOR_WHITE_GREEN   "\033[38;5;15;48;5;22m"
#define COLOR_BLACK_YELLOW  "\033[38;5;0;48;5;221m"

struct screen_layout {
	unsigned int rows;
	unsigned int columns;
	unsigned int station_rows;
	unsigned int status_row;
	unsigned int track_row;
	unsigned int message_row;
	unsigned int prompt_row;
	int too_small;
};

enum playback_state {
	PLAYBACK_STOPPED,
	PLAYBACK_PLAYING,
	PLAYBACK_PAUSED
};

enum screen_view {
	VIEW_STATIONS,
	VIEW_HELP
};

static const char *const help_lines[] = {
	"Commands:",
	"  Enter    play highlighted station",
	"  Up/Down  select station / scroll help",
	"  1-99     play station number",
	"  p        pause/resume",
	"  s        stop",
	"  m        mute/unmute",
	"  v N      set volume to 0-100",
	"  :load pathname  load PLS playlist (spaces allowed)",
	"  :default        save current playlist as default",
	"  l        show station list",
	"  h, ?     toggle help",
	"  q        quit"
};

#define HELP_COUNT (sizeof(help_lines) / sizeof(help_lines[0]))

static struct termios saved_termios;
static int terminal_active;
static volatile sig_atomic_t resize_pending;

static void
handle_sigwinch(int signo)
{
	(void)signo;
	resize_pending = 1;
}

static int
terminal_enter(void)
{
	struct termios termios;

	if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
		return -1;

	if (tcgetattr(STDIN_FILENO, &saved_termios) == -1)
		return -1;

	termios = saved_termios;
	termios.c_lflag &= ~(ICANON | ECHO);
	termios.c_cc[VMIN] = 1;
	termios.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termios) == -1)
		return -1;

	terminal_active = 1;
	return 0;
}

static void
terminal_leave(void)
{
	if (!terminal_active)
		return;

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
	terminal_active = 0;
}

static void
terminal_move(unsigned int row, unsigned int column)
{
	printf("\033[%u;%uH", row, column);
}

static unsigned int
terminal_width(void)
{
	struct winsize size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 ||
	    size.ws_col == 0)
		return 80;

	return size.ws_col;
}

static unsigned int
terminal_height(void)
{
	struct winsize size;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == -1 ||
	    size.ws_row == 0)
		return 24;

	return size.ws_row;
}

static void
draw_header(void)
{
	unsigned int width;
	unsigned int column;

	width = terminal_width();

	terminal_move(1, 3);
	printf("%s\033[1mminimus %s%s",
	    COLOR_YELLOW, MINIMUS_VERSION, COLOR_RESET);

	/*
	 * Draw the separator using the terminal's
	 * DEC special graphics character set.
	 */
	terminal_move(2, 1);
	printf("\033(0");

	for (column = 1; column <= width; column++)
		putchar('q');

	printf("\033(B");
}

#define STATUS_LABEL_WIDTH 9

static void
draw_status_label(const char *label, const char *color)
{
	unsigned int length;
	unsigned int padding;

	length = (unsigned int)strlen(label);
	padding = length < STATUS_LABEL_WIDTH ?
	    STATUS_LABEL_WIDTH - length : 0;

	printf("%s%*s%s %s ",
	    color, (int)padding, "", label, COLOR_RESET);
}

static unsigned int
wrapped_height(const char *label, const char *text,
    unsigned int width)
{
	unsigned int prefix;
	unsigned int available;
	unsigned int lines;
	size_t length;
	size_t offset;
	size_t chunk;
	size_t break_at;

	/*
	 * Leave the final terminal column unused to avoid
	 * triggering automatic line wrapping.
	 */
	if (width > 1)
		width--;

	prefix = (unsigned int)strlen(label);

	if (width <= prefix)
		width = prefix + 1;

	available = width - prefix;
	length = strlen(text);
	offset = 0;
	lines = 1;

	while (offset < length) {
		chunk = length - offset;

		if (chunk > available) {
			chunk = available;
			break_at = chunk;

			while (break_at > 0 &&
			    text[offset + break_at] != ' ')
				break_at--;

			if (break_at > 0)
				chunk = break_at;
		}

		offset += chunk;

		while (offset < length && text[offset] == ' ')
			offset++;

		if (offset < length)
			lines++;
	}

	return lines;
}

static unsigned int
draw_wrapped(unsigned int row, const char *label,
    const char *text, const char *color)
{
	unsigned int width;
	unsigned int prefix;
	unsigned int available;
	unsigned int lines;
	size_t length;
	size_t offset;
	size_t chunk;
	size_t break_at;

	width = terminal_width();

	/*
	 * Match wrapped_height(): reserve the final column.
	 */
	if (width > 1)
		width--;

	prefix = STATUS_LABEL_WIDTH + 1;

	if (width <= prefix)
		width = prefix + 1;

	available = width - prefix;
	length = strlen(text);
	offset = 0;
	lines = 1;

	terminal_move(row, 1);
	printf("\033[2K");
	draw_status_label(label, color);

	while (offset < length) {
		chunk = length - offset;

		if (chunk > available) {
			chunk = available;
			break_at = chunk;

			while (break_at > 0 &&
			    text[offset + break_at] != ' ')
				break_at--;

			if (break_at > 0)
				chunk = break_at;
		}

		printf("%.*s", (int)chunk, text + offset);
		offset += chunk;

		while (offset < length && text[offset] == ' ')
			offset++;

		if (offset < length) {
			lines++;
			terminal_move(row + lines - 1, 1);
			printf("\033[2K%*s", (int)prefix, "");
		}
	}

	fflush(stdout);
	return lines;
}

static unsigned int
draw_station_wrapped(unsigned int row, const char *label,
    const char *text, int highlighted, int playing)
{
	unsigned int width;
	unsigned int prefix;
	unsigned int available;
	unsigned int lines;
	size_t length;
	size_t offset;
	size_t chunk;
	size_t break_at;

	width = terminal_width();

	if (width > 1)
		width--;

	prefix = (unsigned int)strlen(label);

	/*
	 * Reserve two additional columns for the spaces
	 * surrounding highlighted text.
	 */
	if (width <= prefix + 2)
		width = prefix + 3;

	available = width - prefix - 2;
	length = strlen(text);
	offset = 0;
	lines = 1;

	terminal_move(row, 1);
	printf("\033[2K");

	if (playing)
		printf("%s%c%s%s", COLOR_YELLOW, label[0],
		    COLOR_RESET, label + 1);
	else
		printf("%s", label);

	while (offset < length) {
		chunk = length - offset;

		if (chunk > available) {
			chunk = available;
			break_at = chunk;

			while (break_at > 0 &&
			    text[offset + break_at] != ' ')
				break_at--;

			if (break_at > 0)
				chunk = break_at;
		}

		if (highlighted)
			printf("%s", playing ?
			    COLOR_BLACK_YELLOW : COLOR_WHITE_BLUE);

		printf(" %.*s ", (int)chunk, text + offset);

		if (highlighted)
			printf("%s", COLOR_RESET);

		offset += chunk;

		while (offset < length && text[offset] == ' ')
			offset++;

		if (offset < length) {
			lines++;
			terminal_move(row + lines - 1, 1);
			printf("\033[2K%*s", (int)prefix, "");
		}
	}

	fflush(stdout);
	return lines;
}

static unsigned int
station_wrapped_height(const char *label, const char *text,
    unsigned int width)
{
	if (width > 2)
		width -= 2;

	return wrapped_height(label, text, width);
}

static void
draw_input(unsigned int row, const char *input)
{
	unsigned int width;
	unsigned int available;
	size_t length;

	width = terminal_width();

	terminal_move(row, 1);
	printf("\033[2K");

	if (width <= 3) {
		fflush(stdout);
		return;
	}

	/*
	 * Reserve two columns for "> " and one for
	 * the terminal's automatic-wrap boundary.
	 */
	available = width - 3;
	length = strlen(input);

	if (length > available)
		input += length - available;

	printf("> %s", input);
	fflush(stdout);
}

static void
draw_message(unsigned int row, const char *message)
{
	unsigned int width;

	width = terminal_width();

	terminal_move(row, 1);
	printf("\033[2K");

	if (width > 1)
		printf("%.*s", (int)(width - 1), message);

	fflush(stdout);
}

static void
calculate_layout(struct screen_layout *layout,
    const struct pls_playlist *playlist, size_t index,
    enum playback_state state, const char *track)
{
	const char *station_title;
	const char *track_label;
	unsigned int playing_lines;
	unsigned int track_lines;
	unsigned int reserved_rows;

	layout->rows = terminal_height();
	layout->columns = terminal_width();
	layout->station_rows = 0;
	layout->status_row = 0;
	layout->track_row = 0;
	layout->message_row = 0;
	layout->prompt_row = 0;
	layout->too_small = 0;

	if (layout->columns < 20 || layout->rows < 5) {
		layout->too_small = 1;
		return;
	}

	if (index < playlist->count) {
		station_title = playlist->stations[index].title != NULL ?
		    playlist->stations[index].title : "(untitled)";
	} else {
		station_title = "None";
	}

	playing_lines = wrapped_height("          ",
	    station_title, layout->columns);

	if (state == PLAYBACK_STOPPED) {
		track_lines = 1;
	} else {
		track_label = "          ";

		track_lines = wrapped_height(track_label, track,
		    layout->columns);
	}

	/*
	 * Reserve one separator row, one volume row,
	 * the wrapped Playing and track fields, and
	 * one command-prompt row.
	 */
	reserved_rows = HEADER_ROWS + 1 + 1 + playing_lines +
	    track_lines + 1 + 1;

	if (reserved_rows >= layout->rows) {
		layout->too_small = 1;
		return;
	}

	layout->station_rows = layout->rows - reserved_rows;

	/*
	 * The separator occupies the row immediately
	 * above the volume display.
	 */
	layout->status_row = HEADER_ROWS + layout->station_rows + 2;
	layout->track_row = layout->status_row + 1 + playing_lines;
	layout->message_row = layout->track_row + track_lines;
	layout->prompt_row = layout->message_row + 1;
}

static void
draw_station_list(const struct pls_playlist *playlist, size_t index,
    size_t selected_index, size_t *first_station,
    unsigned int available_rows, enum playback_state state)
{
	char label[32];
	const char *title;
	unsigned int width;
	unsigned int used;
	unsigned int lines;
	unsigned int capacity;
	unsigned int row;
	size_t first;
	size_t last;
	size_t i;

	if (available_rows == 0 || playlist->count == 0)
		return;

	width = terminal_width();
	first = *first_station;

	if (first >= playlist->count)
		first = playlist->count - 1;

	if (selected_index < first)
		first = selected_index;

	/*
	 * Prefer displaying station 1 over an upper overflow
	 * indicator when the selection is station 2.
	 */
	if (selected_index == 1 && first == 1)
		first = 0;

	/*
	 * Fill the viewport from its first station, reserving
	 * rows for the upper and lower overflow indicators.
	 * If the selection does not fit, advance the viewport.
	 */
	for (;;) {
		used = 0;
		last = first;
		capacity = available_rows - (first > 0 ? 1U : 0U);

		while (last < playlist->count) {
			i = last;

			snprintf(label, sizeof(label), "%c%2lu  ",
			    i == index ? '*' : ' ',
			    (unsigned long)(i + 1));

			title = playlist->stations[i].title != NULL ?
			    playlist->stations[i].title : "(untitled)";

			lines = station_wrapped_height(label, title, width);

			if (used + lines > capacity)
				break;

			used += lines;
			last++;
		}

		/*
		 * If more stations remain, reserve a lower
		 * indicator row and recalculate the visible end.
		 */
		if (last < playlist->count && used == capacity) {
			last = first;
			used = 0;
			capacity--;

			while (last < playlist->count) {
				i = last;

				snprintf(label, sizeof(label), "%c%2lu  ",
				    i == index ? '*' : ' ',
				    (unsigned long)(i + 1));

				title = playlist->stations[i].title != NULL ?
				    playlist->stations[i].title : "(untitled)";

				lines = station_wrapped_height(label, title,
				    width);

				if (used + lines > capacity)
					break;

				used += lines;
				last++;
			}
		}

		if (selected_index < last)
			break;

		if (first >= selected_index)
			break;

		first++;
	}

	*first_station = first;

	row = HEADER_ROWS + 1;

	if (first > 0) {
		terminal_move(row++, 1);
		printf("\033[2K  ^ %lu station%s above",
		    (unsigned long)first, first == 1 ? "" : "s");
	}

	for (i = first; i < last; i++) {
		snprintf(label, sizeof(label), "%c%2lu  ",
		    i == index ? '*' : ' ',
		    (unsigned long)(i + 1));

		title = playlist->stations[i].title != NULL ?
		    playlist->stations[i].title : "(untitled)";

		row += draw_station_wrapped(row, label, title,
		    i == selected_index,
		    i == index && state != PLAYBACK_STOPPED);
	}

	if (last < playlist->count &&
	    row <= HEADER_ROWS + available_rows) {
		terminal_move(HEADER_ROWS + available_rows, 1);
		printf("\033[2K  v %lu more station%s",
		    (unsigned long)(playlist->count - last),
		    playlist->count - last == 1 ? "" : "s");
	}

	fflush(stdout);
}

static unsigned int
draw_status(const struct pls_playlist *playlist, size_t index,
    unsigned int status_row, unsigned int volume)
{
	const struct pls_station *station;
	unsigned int playing_lines;
	unsigned int track_row;

	station = index < playlist->count ?
	    &playlist->stations[index] : NULL;

	terminal_move(status_row, 1);
	printf("\033[2K");
	draw_status_label("Volume:", COLOR_WHITE_GREEN);
	printf("%u%%", volume);

	playing_lines = draw_wrapped(status_row + 1,
	    "Playing:",
	    station == NULL ? "None" :
	    station->title != NULL ? station->title : "(untitled)",
	    COLOR_WHITE_BLUE);

	track_row = status_row + 1 + playing_lines;

	fflush(stdout);
	return track_row;
}

static void draw_help(unsigned int, unsigned int);

static void
draw_screen(const struct pls_playlist *playlist, size_t index,
    size_t selected_index, size_t *first_station,
    unsigned int normal_volume, int muted,
    enum playback_state state, const char *track, const char *input,
    const char *message, unsigned int *status_row,
    unsigned int *track_row, unsigned int *prompt_row,
    enum screen_view view, unsigned int help_offset)
{
	struct screen_layout layout;
	const char *label;

	calculate_layout(&layout, playlist, index, state, track);

	printf("\033[2J\033[H");

	if (layout.too_small) {
		printf("Terminal too small");
		fflush(stdout);
		return;
	}

	draw_header();

	if (view == VIEW_HELP) {
		draw_help(layout.station_rows, help_offset);
	} else {
		draw_station_list(playlist, index, selected_index,
		    first_station, layout.station_rows, state);
	}

	*status_row = layout.status_row;

	*track_row = draw_status(playlist, index,
	    *status_row, normal_volume);

	if (muted) {
		terminal_move(*status_row, 1);
		printf("\033[2K");
		draw_status_label("Volume:", COLOR_WHITE_GREEN);
		printf("Mute");
	}

	terminal_move(*track_row, 1);
	printf("\033[J");

	if (state == PLAYBACK_STOPPED) {
		printf("\033[2K%*s", STATUS_LABEL_WIDTH,
		    "Stopped");
		*prompt_row = *track_row + 2;
	} else {
		label = state == PLAYBACK_PAUSED ?
		    "Paused:" : "Track:";

		*prompt_row = *track_row +
		    draw_wrapped(*track_row, label, track,
		    COLOR_WHITE_BLUE) + 1;
	}

	draw_message(*prompt_row - 1, message);
	draw_input(*prompt_row, input);
}

static void
draw_help(unsigned int available_rows, unsigned int offset)
{
	unsigned int row;
	unsigned int i;

	for (row = 0; row < available_rows; row++) {
		i = offset + row;

		if (i >= HELP_COUNT)
			break;

		terminal_move(HEADER_ROWS + row + 1, 1);
		printf("\033[2K%.*s",
		    (int)(terminal_width() - 1), help_lines[i]);
	}

	fflush(stdout);
}

static int
parse_metadata(const char *line, char *track, size_t size)
{
	const char *prefix = "@I ICY-META: StreamTitle='";
	const char *start;
	const char *end;
	size_t length;

	if (strncmp(line, prefix, strlen(prefix)) != 0)
		return -1;

	start = line + strlen(prefix);
	end = strstr(start, "';");

	if (end == NULL)
		return -1;

	length = (size_t)(end - start);
	if (length >= size)
		length = size - 1;

	memcpy(track, start, length);
	track[length] = '\0';

	return 0;
}

static int
parse_station_number(const char *s, size_t count, size_t *index)
{
	char *end;
	unsigned long number;

	errno = 0;
	number = strtoul(s, &end, 10);

	if (errno != 0 || *s == '\0' ||
	    (*end != '\0' && *end != '\n') ||
	    number == 0 || number > count)
		return -1;

	*index = (size_t)(number - 1);
	return 0;
}

static int
parse_volume(const char *s, unsigned int *volume)
{
	char *end;
	unsigned long number;

	while (*s == ' ' || *s == '\t')
		s++;

	errno = 0;
	number = strtoul(s, &end, 10);

	if (errno != 0 || *s == '\0' || number > 100)
		return -1;

	while (*end == ' ' || *end == '\t')
		end++;

	if (*end != '\0' && *end != '\n')
		return -1;

	*volume = (unsigned int)number;
	return 0;
}

int
main(int argc, char *argv[])
{
	struct pls_playlist playlist;
	struct pls_playlist new_playlist;
	struct player player;
	struct sigaction action;
	char saved_playlist[1024];
	char current_playlist[1024];
	const char *playlist_path;
	fd_set readfds;
	char command[COMMAND_SIZE];
	char input[INPUT_SIZE];
	char output[OUTPUT_SIZE];
	char track[TRACK_SIZE];
	char message[MESSAGE_SIZE];
	char ch;
	int escape_state = 0;
	size_t input_length = 0;
	size_t index;
	size_t selected_index;
	size_t first_station;
	unsigned int status_row;
	unsigned int track_row;
	unsigned int prompt_row;
	unsigned int help_rows;
	unsigned int volume;
	unsigned int normal_volume;
	int muted = 0;
	ssize_t n;
	int maxfd;
	enum playback_state state = PLAYBACK_STOPPED;
	enum screen_view view = VIEW_STATIONS;
	unsigned int help_offset = 0;
	int running = 1;

	if (argc == 2 && strcmp(argv[1], "-h") == 0) {
		printf("Usage: minimus [playlist.pls]\n");
		return 0;
	}

	if (argc > 2) {
		fprintf(stderr, "Usage: minimus [playlist.pls]\n");
		return 1;
	}

	if (state_load_playlist(saved_playlist,
	    sizeof(saved_playlist)) == -1) {
		fprintf(stderr, "unable to load default playlist setting\n");
		return 1;
	}

	if (argc == 2)
		playlist_path = argv[1];
	else if (saved_playlist[0] != '\0')
		playlist_path = saved_playlist;
	else
		playlist_path = DEFAULT_PLAYLIST;

	if (state_load_volume(&normal_volume) == -1) {
		fprintf(stderr, "unable to load state\n");
		return 1;
	}

	if (pls_load(playlist_path, &playlist) != 0) {
		fprintf(stderr, "unable to load playlist: %s\n",
		    playlist_path);
		return 1;
	}

	if (snprintf(current_playlist, sizeof(current_playlist),
	    "%s", playlist_path) >= (int)sizeof(current_playlist)) {
		fprintf(stderr, "playlist pathname too long\n");
		pls_free(&playlist);
		return 1;
	}

	if (player_start(&player) == -1) {
		perror("player_start");
		pls_free(&playlist);
		return 1;
	}

	if (snprintf(command, sizeof(command), "V %u\n",
	    normal_volume) >= (int)sizeof(command)) {
		fprintf(stderr, "unable to set initial volume\n");
		player_stop(&player);
		pls_free(&playlist);
		return 1;
	}

	if (player_send(&player, command) == -1) {
		perror("player_send");
		player_stop(&player);
		pls_free(&playlist);
		return 1;
	}

	/*
	 * Start with the first station highlighted, but do not
	 * begin playback until the user selects a station.
	 */
	index = playlist.count;
	selected_index = 0;
	first_station = 0;

	maxfd = player.output_fd > STDIN_FILENO ?
	    player.output_fd : STDIN_FILENO;

	if (terminal_enter() == -1) {
		perror("terminal_enter");
		player_stop(&player);
		pls_free(&playlist);
		return 1;
	}

	memset(&action, 0, sizeof(action));
	action.sa_handler = handle_sigwinch;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;

	if (sigaction(SIGWINCH, &action, NULL) == -1) {
		terminal_leave();
		perror("sigaction");
		player_stop(&player);
		pls_free(&playlist);
		return 1;
	}

	track[0] = '\0';
	input[0] = '\0';
	message[0] = '\0';

	draw_screen(&playlist, index, selected_index, &first_station,
	    normal_volume, muted, state, track, input, message,
	    &status_row, &track_row, &prompt_row, view, help_offset);

	while (running) {
		if (resize_pending) {
			resize_pending = 0;

			draw_screen(&playlist, index, selected_index, &first_station,
			    normal_volume, muted, state, track, input, message,
			    &status_row, &track_row, &prompt_row, view, help_offset);
		}

		FD_ZERO(&readfds);
		FD_SET(STDIN_FILENO, &readfds);
		FD_SET(player.output_fd, &readfds);

		if (select(maxfd + 1, &readfds, NULL, NULL, NULL) == -1) {
			if (errno == EINTR)
				continue;

			perror("select");
			break;
		}

		if (FD_ISSET(player.output_fd, &readfds)) {
			n = player_read_line(&player, output, sizeof(output));
			if (n == -1) {
				perror("player_read_line");
				break;
			}

			if (n == 0) {
				fprintf(stderr, "mpg123 exited unexpectedly\n");
				break;
			}

			if (strncmp(output, "@I ICY-META: ", 13) == 0) {
				if (parse_metadata(output, track,
				    sizeof(track)) == 0) {
					draw_screen(&playlist, index,
					    selected_index, &first_station,
					    normal_volume, muted,
					    state, track, input,
					    message, &status_row,
					    &track_row, &prompt_row,
					    view, help_offset);
				}
			} else if (strcmp(output, "@P 0") == 0) {
				state = PLAYBACK_STOPPED;

				draw_screen(&playlist, index,
				    selected_index, &first_station,
				    normal_volume, muted,
				    state, track, input,
				    message, &status_row,
				    &track_row, &prompt_row,
				    view, help_offset);
			} else if (strcmp(output, "@P 1") == 0) {
				state = PLAYBACK_PAUSED;

				draw_screen(&playlist, index,
				    selected_index, &first_station,
				    normal_volume, muted,
				    state, track, input,
				    message, &status_row,
				    &track_row, &prompt_row,
				    view, help_offset);
			} else if (strcmp(output, "@P 2") == 0) {
				state = PLAYBACK_PLAYING;

				draw_screen(&playlist, index,
				    selected_index, &first_station,
				    normal_volume, muted,
				    state, track, input,
				    message, &status_row,
				    &track_row, &prompt_row,
				    view, help_offset);
			} else if (strncmp(output, "@V ", 3) == 0) {
				terminal_move(status_row, 1);
				printf("\033[2K");
				draw_status_label("Volume:", COLOR_WHITE_GREEN);

				if (muted)
					printf("Mute");
				else
					printf("%.0f%%", atof(output + 3));
			} else if (strncmp(output, "@R ", 3) == 0 ||
			    strncmp(output, "@S ", 3) == 0 ||
			    strncmp(output, "@F ", 3) == 0 ||
			    strncmp(output, "@I ", 3) == 0) {
				/* Routine mpg123 protocol output. */
			} else {
				printf("%s\n", output);
			}

			fflush(stdout);
			draw_input(prompt_row, input);
		}

		if (FD_ISSET(STDIN_FILENO, &readfds)) {
			n = read(STDIN_FILENO, &ch, 1);
			if (n == -1) {
				if (errno == EINTR)
					continue;

				perror("read");
				running = 0;
			} else if (n == 0) {
				running = 0;
			} else if (escape_state == 1) {
				escape_state = ch == '[' ? 2 : 0;
				continue;
			} else if (escape_state == 2) {
				escape_state = 0;

				if (view == VIEW_HELP) {
					/*
					 * The station list and help view share
					 * the area above the status separator.
					 */
					help_rows = status_row - 2;

					if (ch == 'A' && help_offset > 0)
						help_offset--;
					else if (ch == 'B' &&
					    HELP_COUNT > help_rows &&
					    help_offset <
					    HELP_COUNT - help_rows)
						help_offset++;
					else
						continue;
				} else {
					if (ch == 'A' && selected_index > 0)
						selected_index--;
					else if (ch == 'B' &&
					    selected_index + 1 < playlist.count)
						selected_index++;
					else
						continue;
				}

				draw_screen(&playlist, index, selected_index, &first_station,
				    normal_volume, muted, state, track, input, message,
				    &status_row, &track_row, &prompt_row, view, help_offset);
				continue;
			} else if (ch == '\033') {
				escape_state = 1;
				continue;
			} else if (ch == '\b' || ch == '\177') {
				if (input_length > 0) {
					input_length--;
					input[input_length] = '\0';
					draw_input(prompt_row, input);
				}
				continue;
			} else if (ch != '\n' && ch != '\r') {
				if (input_length < sizeof(input) - 2) {
					input[input_length++] = ch;
					input[input_length] = '\0';
					draw_input(prompt_row, input);
				}
				continue;
			} else {
				input[input_length++] = '\n';
				input[input_length] = '\0';
				input_length = 0;
				message[0] = '\0';

				terminal_move(prompt_row, 1);
				printf("\033[2K");
				fflush(stdout);

				if (strcmp(input, "\n") == 0) {
					if (snprintf(command, sizeof(command),
					    "L %s\n",
					    playlist.stations[selected_index].url) >=
					    (int)sizeof(command)) {
						snprintf(message, sizeof(message),
						    "Station URL is too long");
					} else if (player_send(&player,
					    command) == -1) {
						perror("player_send");
						running = 0;
					} else {
						index = selected_index;
						track[0] = '\0';

						draw_screen(&playlist, index,
						    selected_index, &first_station,
						    normal_volume, muted,
						    state, track, "", message,
						    &status_row, &track_row,
						    &prompt_row, view, help_offset);
					}
				} else if (strcmp(input, "q\n") == 0) {
					running = 0;
				} else if (strcmp(input, "p\n") == 0 ||
				    strcmp(input, "P\n") == 0) {
					if (player_send(&player, "P\n") == -1) {
						perror("player_send");
						running = 0;
					}
				} else if (strcmp(input, "s\n") == 0 ||
				    strcmp(input, "S\n") == 0) {
					if (player_send(&player, "S\n") == -1) {
						perror("player_send");
						running = 0;
					}
				} else if (strcmp(input, "m\n") == 0 ||
				    strcmp(input, "M\n") == 0) {
					if (muted) {
						snprintf(command, sizeof(command),
						    "V %u\n", normal_volume);
					} else {
						snprintf(command, sizeof(command),
						    "V 0\n");
					}

					if (player_send(&player, command) == -1) {
						perror("player_send");
						running = 0;
					} else {
						muted = !muted;
					}
				} else if (strcmp(input, "l\n") == 0 ||
				    strcmp(input, "L\n") == 0) {
					view = VIEW_STATIONS;
					draw_screen(&playlist, index,
					    selected_index, &first_station,
					    normal_volume, muted, state, track,
					    "", message, &status_row, &track_row,
					    &prompt_row, view, help_offset);
				} else if (strcmp(input, "h\n") == 0 ||
				    strcmp(input, "H\n") == 0 ||
				    strcmp(input, "?\n") == 0) {
					view = view == VIEW_HELP ?
					    VIEW_STATIONS : VIEW_HELP;
					help_offset = 0;
					draw_screen(&playlist, index,
					    selected_index, &first_station,
					    normal_volume, muted, state, track,
					    "", message, &status_row, &track_row,
					    &prompt_row, view, help_offset);
				} else if (strcmp(input, ":default\n") == 0) {
					if (state_save_playlist(current_playlist) == -1) {
						snprintf(message, sizeof(message),
						    "Unable to save default playlist");
					} else {
						snprintf(message, sizeof(message),
						    "Default playlist saved: %.90s",
						    current_playlist);
					}
				} else if (strcmp(input, ":load\n") == 0 ||
				    strncmp(input, ":load ", 6) == 0) {
					char *path;

					path = input + 6;
					path[strcspn(path, "\r\n")] = '\0';

					if (*path == '\0') {
						snprintf(message, sizeof(message),
						    "Playlist pathname required");
					} else if (strlen(path) >=
					    sizeof(current_playlist)) {
						snprintf(message, sizeof(message),
						    "Playlist pathname too long");
					} else if (pls_load(path, &new_playlist) == -1) {
						snprintf(message, sizeof(message),
						    "Unable to load playlist: %.90s", path);
					} else if (new_playlist.count == 0) {
						pls_free(&new_playlist);
						snprintf(message, sizeof(message),
						    "Playlist contains no stations");
					} else if (player_send(&player, "S\n") == -1) {
						perror("player_send");
						pls_free(&new_playlist);
						running = 0;
					} else {
						pls_free(&playlist);
						playlist = new_playlist;
						strcpy(current_playlist, path);

						index = playlist.count;
						selected_index = 0;
						first_station = 0;
						state = PLAYBACK_STOPPED;
						track[0] = '\0';
						view = VIEW_STATIONS;
						help_offset = 0;

						snprintf(message, sizeof(message),
						    "Playlist loaded: %.90s", path);

						draw_screen(&playlist, index,
						    selected_index, &first_station,
						    normal_volume, muted, state, track,
						    "", message, &status_row, &track_row,
						    &prompt_row, view, help_offset);
					}
				} else if (input[0] == 'v' ||
				    input[0] == 'V') {
					if (parse_volume(input + 1,
					    &volume) == -1) {
						snprintf(message, sizeof(message),
						    "Volume must be between 0 and 100");
					} else {
						snprintf(command, sizeof(command),
						    "V %u\n", volume);

						if (player_send(&player,
						    command) == -1) {
							perror("player_send");
							running = 0;
						} else {
							normal_volume = volume;
							muted = 0;

							if (state_save_volume(
							    normal_volume) == -1)
								fprintf(stderr,
								    "unable to save state\n");
						}
					}
				} else if (parse_station_number(input,
				    playlist.count, &index) == 0) {
					if (snprintf(command, sizeof(command),
					    "L %s\n",
					    playlist.stations[index].url) >=
					    (int)sizeof(command)) {
						snprintf(message, sizeof(message),
						    "Station URL is too long");
					} else if (player_send(&player,
					    command) == -1) {
						perror("player_send");
						running = 0;
					} else {
						selected_index = index;
						track[0] = '\0';

						draw_screen(&playlist, index,
						    selected_index, &first_station,
						    normal_volume, muted,
						    state, track, "", message,
						    &status_row, &track_row,
						    &prompt_row, view, help_offset);
					}
				} else {
					snprintf(message, sizeof(message),
					    "Unknown command; type h for help");
				}

				if (running) {
					input[0] = '\0';
					draw_message(prompt_row - 1, message);
					draw_input(prompt_row, input);
				}
			}
		}
	}

	terminal_leave();

	if (player_stop(&player) == -1) {
		perror("player_stop");
		pls_free(&playlist);
		return 1;
	}

	pls_free(&playlist);
	return 0;
}
