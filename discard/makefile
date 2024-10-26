CC = gcc

CFLAGS = -g -Wall -Wshadow -Wvla
DFLAGS = -DDEBUG


#-- source files, object files and binaries
P1_c = P1.c
P2_c = P2.c
P3_c = P3.c

stub_o = stub.o
P1_o = P1.o
P2_o = P2.o
P3_o = P3.o

bin_P1 = P1
bin_P2 = P2
bin_p3 = P3


all: $(bin_P1) $(bin_P2) $(bin_p3)


# creates stub.o
stubo: stub.c
	$(CC) $(CFLAGS) -c stub.c -o $(stub_o)


#-- P1:
$(P1_o): $(P1_c)
	$(CC) $(CFLAGS) -c $(P1_c) -o $(P1_o)
# $(P1_o): $(P1_c)
# 	$(CC) $(CFLAGS) $(DFLAGS) -c $(P1_c) -o $(P1_o)

$(bin_P1): $(P1_o) $(stub_o)
	$(CC) $(CFLAGS) $(P1_o) $(stub_o) -o $(bin_P1)


#-- P2:
$(P2_o): $(P2_c)
	$(CC) $(CFLAGS) -c $(P2_c) -o $(P2_o)
# $(P2_o): $(P2_c)
# 	$(CC) $(CFLAGS) $(DFLAGS) -c $(P2_c) -o $(P2_o)

$(bin_P2): $(P2_o) $(stub_o)
	$(CC) $(CFLAGS) $(P2_o) $(stub_o) -o $(bin_P2)


#-- P3:
$(P3_o): $(P3_c)
	$(CC) $(CFLAGS) -c $(P3_c) -o $(P3_o)
# $(P3_o): $(P3_c)
# 	$(CC) $(CFLAGS) $(DFLAGS) -c $(P3_c) -o $(P3_o)

$(bin_p3): $(P3_o) $(stub_o)
	$(CC) $(CFLAGS) $(P3_o) $(stub_o) -o $(bin_p3)


# removes executables and object files
clean:
	rm -f $(P1_o) $(P2_o) $(P3_o) $(stub_o) $(bin_P1) $(bin_P2) $(bin_p3)
