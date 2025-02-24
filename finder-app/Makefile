CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror

all:
	$(CC) $(CFLAGS) -c -o writer.o writer.c
	$(CC) $(CFLAGS) -I/ writer.o -o writer

clean:
	rm -f *.o writer