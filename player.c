#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "player.h"

int
player_start(struct player *player)
{
	int to_mpg123[2];
	int from_mpg123[2];
	pid_t pid;

	if (player == NULL) {
		errno = EINVAL;
		return -1;
	}

	player->pid = -1;
	player->input_fd = -1;
	player->output_fd = -1;
	player->line_length = 0;

	if (pipe(to_mpg123) == -1)
		return -1;

	if (pipe(from_mpg123) == -1) {
		close(to_mpg123[0]);
		close(to_mpg123[1]);
		return -1;
	}

	pid = fork();
	if (pid == -1) {
		close(to_mpg123[0]);
		close(to_mpg123[1]);
		close(from_mpg123[0]);
		close(from_mpg123[1]);
		return -1;
	}

	if (pid == 0) {
		close(to_mpg123[1]);
		close(from_mpg123[0]);

		if (dup2(to_mpg123[0], STDIN_FILENO) == -1) {
			perror("dup2 stdin");
			_exit(1);
		}

		if (dup2(from_mpg123[1], STDOUT_FILENO) == -1) {
			perror("dup2 stdout");
			_exit(1);
		}

		close(to_mpg123[0]);
		close(from_mpg123[1]);

		execlp("mpg123", "mpg123", "-R", "-o", "minix",
		    (char *)NULL);

		perror("execlp mpg123");
		_exit(1);
	}

	close(to_mpg123[0]);
	close(from_mpg123[1]);

	player->pid = pid;
	player->input_fd = to_mpg123[1];
	player->output_fd = from_mpg123[0];

	return 0;
}

int
player_send(struct player *player, const char *command)
{
	size_t length;
	size_t written = 0;
	ssize_t n;

	if (player == NULL || command == NULL || player->input_fd == -1) {
		errno = EINVAL;
		return -1;
	}

	length = strlen(command);

	while (written < length) {
		n = write(player->input_fd, command + written,
		    length - written);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}

		written += (size_t)n;
	}

	return 0;
}

ssize_t
player_read(struct player *player, char *buffer, size_t size)
{
	ssize_t n;

	if (player == NULL || buffer == NULL || size == 0 ||
	    player->output_fd == -1) {
		errno = EINVAL;
		return -1;
	}

	do {
		n = read(player->output_fd, buffer, size);
	} while (n == -1 && errno == EINTR);

	return n;
}

ssize_t
player_read_line(struct player *player, char *buffer, size_t size)
{
	char *newline;
	size_t line_length;
	size_t remaining;
	ssize_t n;

	if (player == NULL || buffer == NULL || size < 2 ||
	    player->output_fd == -1) {
		errno = EINVAL;
		return -1;
	}

	for (;;) {
		newline = memchr(player->line_buffer, '\n',
		    player->line_length);

		if (newline != NULL) {
			line_length = (size_t)(newline - player->line_buffer);

			if (line_length >= size)
				line_length = size - 1;

			memcpy(buffer, player->line_buffer, line_length);
			buffer[line_length] = '\0';

			remaining = player->line_length -
			    ((size_t)(newline - player->line_buffer) + 1);

			memmove(player->line_buffer, newline + 1, remaining);
			player->line_length = remaining;

			return (ssize_t)line_length;
		}

		if (player->line_length == sizeof(player->line_buffer)) {
			errno = EMSGSIZE;
			return -1;
		}

		do {
			n = read(player->output_fd,
			    player->line_buffer + player->line_length,
			    sizeof(player->line_buffer) -
			    player->line_length);
		} while (n == -1 && errno == EINTR);

		if (n <= 0)
			return n;

		player->line_length += (size_t)n;
	}
}

int
player_stop(struct player *player)
{
	char buffer[256];
	ssize_t n;
	int status;

	if (player == NULL || player->pid == -1) {
		errno = EINVAL;
		return -1;
	}

	if (player_send(player, "QUIT\n") == -1)
		return -1;

	close(player->input_fd);
	player->input_fd = -1;

	do {
		n = read(player->output_fd, buffer, sizeof(buffer));
	} while (n > 0 || (n == -1 && errno == EINTR));

	if (n == -1) {
		close(player->output_fd);
		player->output_fd = -1;
		return -1;
	}

	close(player->output_fd);
	player->output_fd = -1;

	if (waitpid(player->pid, &status, 0) == -1)
		return -1;

	player->pid = -1;

	if (WIFEXITED(status)) {
		if (WEXITSTATUS(status) != 0) {
			fprintf(stderr, "mpg123 exited with status %d\n",
			    WEXITSTATUS(status));
			errno = EIO;
			return -1;
		}
	} else if (WIFSIGNALED(status)) {
		fprintf(stderr, "mpg123 terminated by signal %d\n",
		    WTERMSIG(status));
		errno = EIO;
		return -1;
	} else {
		fprintf(stderr, "mpg123 terminated abnormally\n");
		errno = EIO;
		return -1;
	}

	return 0;
}
