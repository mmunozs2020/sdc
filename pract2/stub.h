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


#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf("DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif


enum operations {
    READY_TO_SHUTDOWN = 0,
    SHUTDOWN_NOW,
    SHUTDOWN_ACK
};

struct message {
    char origin[20];
    enum operations action;
    unsigned int clock_lamport;
};


extern int sock_status;
extern int sock_sfd;

int get_clock_lamport();
int send_msg(const char *from, const char *to, enum operations action);

void terminate_server(int exit_status);
int start_up_server(int argc, char *argv[], char *whoami);

void terminate_client(int exit_status);
int start_up_client(int argc, char *argv[], char *whoami);

#endif // STUB_H
