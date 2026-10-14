CC = cc
CFLAGS = -Wall -Wextra
PREFIX = /usr/local

PROG = minimus
OBJS = minimus.o player.o pls.o state.o
.PHONY: all install clean

all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

minimus.o: minimus.c player.h pls.h state.h
player.o: player.c player.h
pls.o: pls.c pls.h
state.o: state.c state.h

install: $(PROG)
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/minimus
	cp $(PROG) $(PREFIX)/bin/$(PROG)
	chmod 755 $(PREFIX)/bin/$(PROG)
	cp stations.pls $(PREFIX)/share/minimus/stations.pls
	chmod 644 $(PREFIX)/share/minimus/stations.pls

clean:
	rm -f $(PROG) $(OBJS)
