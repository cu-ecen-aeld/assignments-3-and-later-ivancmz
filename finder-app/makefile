CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Werror

all: writer

writer: writer.o
	$(CC) $(CFLAGS) -o $@ $^

writer.o: writer.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f writer *.o