CC = gcc

CFLAGS = -Wall -Wshadow -Wvla
DFLAGS = -DDEBUG

BIN_CLI = client
BIN_SERV = server


all: stub client server


# Stub
stub: stub.c stub.h
	$(CC) -c stub.c -o stub.o $(CFLAGS)
d-stub: stub.c stub.h
	$(CC) -c stub.c -o stub.o $(CFLAGS) $(DFLAGS)


# client:
client: client.c stub.o 
	$(CC) client.c stub.o -o $(BIN_CLI) $(CFLAGS)
d-client: client.c stub.o 
	$(CC) client.c stub.o -o $(BIN_CLI) $(CFLAGS) $(DFLAGS)


# server:
server: server.c stub.o 
	$(CC) server.c stub.o -o $(BIN_SERV) $(CFLAGS)
d-server: server.c stub.o 
	$(CC) server.c stub.o -o $(BIN_SERV) $(CFLAGS) $(DFLAGS)


clean:
	rm -f *.o $(BIN_CLI) $(BIN_SERV) $(BIN_3)
