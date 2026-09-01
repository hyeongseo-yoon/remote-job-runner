CC = gcc
CFLAGS = -Wall -Wextra -g

SRC = job-executor.c
BIN = job-executor

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)

.PHONY: all clean
