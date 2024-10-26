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


#define PORT            8073

#define SOCKET_RUNNING  1
#define SOCKET_CLOSED   -1

#define F_FAILURE       -1
#define F_SUCCESS       0
#define F_CONN_CLOSE    -3

#define WR_SUCCESS      0
#define WR_FAILURE      -1
#define WR_NTR          -2

#define SOCK_EXIT_SUCCESS   10
#define SOCK_EXIT_FAILURE   11
#define SOCK_EXIT_SIGINT    12

#define NUM_OF_CLIENTS      1


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
extern int sock_exit;
extern int l_clock;
extern int sock_sfd;
extern int shutdown_acks;


void handle_sigint(int sig);
void perror_msg(char *msg, int sockfd, int is_socket_running);
#define perror_msg_sr(msg, sockfd) perror_msg(msg, sockfd, SOCKET_RUNNING)

int check_argnum(int argn);
int try_get_port(char *str_port);

int init_socket(struct sockaddr_in *servaddr, char *serv_ip, int serv_port);
void enable_setsockopt(int serv_sfd);
int bind_and_listen(int serv_sfd, struct sockaddr_in *servaddr);
int accept_client(int serv_sfd, struct sockaddr_in *cliaddr);
int connect_to_server(int cli_sfd, struct sockaddr_in *servaddr);

struct message* create_empty_msg();
struct message* create_msg(const char *origin, enum operations action, unsigned int clock);

int send_through_socket(int conn_fd, struct message *msg);
int receive_msg(int conn_fd, struct message *msg);
void print_msg(struct message *msg);
void free_msg(struct message* msg);

void update_clock_lamport(int *l_clock_loc);
int get_clock_lamport();

void terminate_server(int exit_status);
int server_listening(int *cfd);
int start_up_server(int argc, char *argv[]);
int client_listening(int *cfd);
int start_up_client(int argc, char *argv[]);

#endif // STUB_H
