CC = gcc

CFLAGS = -Wall -Wshadow -Wvla
DFLAGS = -DDEBUG

BIN_1 = P1
BIN_2 = P2
BIN_3 = P3


all: stub uno dos tres


# Stub
stub: stub.c stub.h
	$(CC) -c stub.c -o stub.o $(CFLAGS)
d-stub: stub.c stub.h
	$(CC) -c stub.c -o stub.o $(CFLAGS) $(DFLAGS)


# P1:
uno: P1.c stub.o 
	$(CC) P1.c stub.o -o $(BIN_1) $(CFLAGS)
d-uno: P1.c stub.o 
	$(CC) P1.c stub.o -o $(BIN_1) $(CFLAGS) $(DFLAGS)


# P2:
dos: P2.c stub.o 
	$(CC) P2.c stub.o -o $(BIN_2) $(CFLAGS)
d-dos: P2.c stub.o 
	$(CC) P2.c stub.o -o $(BIN_2) $(CFLAGS) $(DFLAGS)


# P3:
tres: P3.c stub.o 
	$(CC) P3.c stub.o -o $(BIN_3) $(CFLAGS)
d-tres: P3.c stub.o 
	$(CC) P3.c stub.o -o $(BIN_3) $(CFLAGS) $(DFLAGS)


clean:
	rm -f *.o $(BIN_1) $(BIN_2) $(BIN_3)
