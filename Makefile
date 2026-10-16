CC = gcc
CFLAGS = -Wall -Wextra -std=c11

simple-shell: simple-shell.c
	$(CC) $(CFLAGS) simple-shell.c -o simple-shell

clean:
	rm -f simple-shell
