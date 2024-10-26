CC = gcc

CFLAGS = -g -Wall -Wshadow -Wvla
LFLAGS = -g
DFLAGS = -DDEBUG

client: client.c 
	$(CC) $(CFLAGS) -c client.c
	$(CC) -o client client.o

d-client: client.c 
	$(CC) $(CFLAGS) $(DFLAGS) -c client.c
	$(CC) -o client client.o

server: server.c
	$(CC) $(CFLAGS) -c server.c
	$(CC) -o server server.o

d-server: server.c
	$(CC) $(CFLAGS) $(DFLAGS) -c server.c
	$(CC) -o server server.o