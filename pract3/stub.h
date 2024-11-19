#ifndef STUB_H
#define STUB_H


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <err.h>
#include <errno.h>

#define F_FAILURE           -1
#define F_SUCCESS           0
#define F_CONN_CLOSE        -3

#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf("DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif

int get_cli_args(int argc, char **argv);
int get_serv_args(int argc, char **argv);
void free_args();
void print_arguments();



void start_up_server(int argc, char *argv[]);
void start_up_client(int argc, char *argv[]);

#endif // STUB_H
