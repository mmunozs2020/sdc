CC = gcc

CFLAGS = -g -Wall -Wshadow -Wvla
LFLAGS = -g

client: client.c 
	$(CC) $(CFLAGS) -c client.c
	$(CC) -o client client.o

server: server.c
	$(CC) $(CFLAGS) -c server.c
	$(CC) -o server server.o