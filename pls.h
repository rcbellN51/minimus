#ifndef PLS_H
#define PLS_H

#include <stddef.h>

struct pls_station {
	char *title;
	char *url;
};

struct pls_playlist {
	struct pls_station *stations;
	size_t count;
};

int	pls_load(const char *, struct pls_playlist *);
void	pls_free(struct pls_playlist *);

#endif
