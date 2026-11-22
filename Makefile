CC = gcc
CFLAGS = -Wall -Wextra -g

all: server client

server: server.c job.c job.h
	$(CC) $(CFLAGS) -o $@ server.c job.c

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c

clean:
	rm -f server client

.PHONY: all clean
