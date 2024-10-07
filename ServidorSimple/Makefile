CC = gcc
CFLAGS = -g -Wall -DDEBUG
DEPS =

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

all: server client

server: server.o
	$(CC) -o server server.o

client: client.o
	$(CC) -o client client.o

clean:
	rm -f *.o server client