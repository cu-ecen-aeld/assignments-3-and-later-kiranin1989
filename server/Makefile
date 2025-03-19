CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror

all:
	$(CC) $(CFLAGS) -c -o aesdsocket.o aesdsocket.c
	$(CC) $(CFLAGS) -I/ aesdsocket.o -o aesdsocket

clean:
	rm -f *.o aesdsocket