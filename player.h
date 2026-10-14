#ifndef PLAYER_H
#define PLAYER_H

#include <sys/types.h>

#define PLAYER_LINE_SIZE 2048

struct player {
	pid_t pid;
	int input_fd;
	int output_fd;
	char line_buffer[PLAYER_LINE_SIZE];
	size_t line_length;
};

int	player_start(struct player *);
int	player_send(struct player *, const char *);
ssize_t	player_read(struct player *, char *, size_t);
ssize_t	player_read_line(struct player *, char *, size_t);
int	player_stop(struct player *);

#endif
