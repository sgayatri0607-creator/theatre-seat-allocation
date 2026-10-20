CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L

all: server

server: server.c theatre.c theatre.h
	$(CC) $(CFLAGS) -o server server.c theatre.c

test: test.c theatre.c theatre.h
	$(CC) $(CFLAGS) -o test_runner test.c theatre.c
	./test_runner

clean:
	rm -f server test_runner server.exe test_runner.exe
