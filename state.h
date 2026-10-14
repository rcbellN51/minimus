#ifndef STATE_H
#define STATE_H

#include <stddef.h>

#define STATE_DEFAULT_VOLUME 50

int	state_load_volume(unsigned int *);
int	state_save_volume(unsigned int);

int	state_load_playlist(char *, size_t);
int	state_save_playlist(const char *);

#endif
