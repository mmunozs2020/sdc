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
#include <pthread.h> 


#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf("DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif


#define SOCKET_RUNNING  1
#define SOCKET_CLOSED   0

#define F_FAILURE       -1
#define F_SUCCESS       0

// WR stands for wait_receive (first words of a function below)
#define WR_SUCCESS      0
#define WR_FAILURE      -1
#define WR_NTR          -2  // NTR stands for Nothing To Read

#define MAX_QUEUEING    1000
#define MAX_PARALLEL    100


// Socket file descriptor for server
int serv_sfd;


//-- Handles SIGINT signals so the SERVER can be stopped with CTRL+C
void 
handle_sigint(int sig)
{
    printf("\nSocket shutdown received (CTRL+C)...\n");
    close(serv_sfd); // Close server socket
    
    exit(EXIT_SUCCESS);
}

//-- Prints the proper error msg and terminates with failure
void 
perror_exit(char *msg, int socket_running)
{
    perror(msg);

    if (socket_running) {
        DEBUG_PRINTF("Closing server socket after perror\n");
        close(serv_sfd);
    }

    exit(EXIT_FAILURE);
}

//-- Calls perror_exit with SOCKET_RUNNING as second parameter
#define perror_exit_sr(msg) perror_exit(msg, SOCKET_RUNNING)

//-- Sets up the server socket configuration
int 
init_server_socket(struct sockaddr_in *servaddr, int port)
{
    const int ENABLE_SSOPT = 1;

    serv_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sfd < 0) {
        perror_exit("Error creating socket", SOCKET_CLOSED);
    }
    
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);  // All interfaces
    servaddr->sin_port = htons(port);

    if (setsockopt(serv_sfd, SOL_SOCKET, SO_REUSEADDR, &ENABLE_SSOPT, sizeof(int)) < 0) {
        perror_exit_sr("setsockopt(SO_REUSEADDR) failed\n");
    }

    printf("Socket successfully created...\n");
    return serv_sfd;
}

//-- Bind and listen on the server socket
void
bind_and_listen(struct sockaddr_in *servaddr) 
{
    if (bind(serv_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_exit_sr("bind failed");
    }
    printf("Socket successfully binded...\n");

    if (listen(serv_sfd, MAX_QUEUEING) < 0) {
        perror_exit_sr("listen failed");
    }
    printf("Server listening...\n");
}

//-- Receives a message from the file descriptor conn_fd and prints it (blocks)
int
receive_msg(int conn_fd, char *buff, size_t buffsize) 
{
    // clear buffer and block until receiving a msg
    memset(buff, 0, buffsize);
    int bytes_received = recv(conn_fd, buff, buffsize, 0);
    
    if (bytes_received < 0) {
        perror("recv failed");
        return F_FAILURE;
    }
    
    // null-terminate the msg and print it after the "+++" indicator
    buff[bytes_received] = '\0';
    printf("+++ %s", buff);
    return bytes_received;
}

//-- Sends a message to the client
int
send_msg(int conn_fd)
{
    char msg[256];

    snprintf(msg, sizeof(msg), "Hello client!\n");

    if (send(conn_fd, msg, strlen(msg), 0) < 0) {
        perror("send failed");
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- Returns a status after receiving n bytes (with n being: int bytes_received)
int
process_bytes_received(int bytes_received)
{
    if (bytes_received < 0) {
        perror("recv failed");
        return WR_FAILURE;
    }

    // In case the client receives a 0 byte msg, it means the server closed
    if (bytes_received == 0) {
        printf("Server closed the connection\n");
        return WR_FAILURE;
    }

    return WR_SUCCESS;
}

//-- Communication between client and server [HERE: server]
void
connection_dialogue(int *cfd)
{
    int conn_fd = *((int*)cfd);  // Extract value of cfd (client file descriptor)
    char conn_buffer[1024];
    int listening = 1;
    double wait_time;

    DEBUG_PRINTF("Server inside connection dialogue (thread)\n");

    while (listening) {

        DEBUG_PRINTF("Server before recv...(), conn_fd = %i (thread)\n", conn_fd);
        
        // Server waits between 0.5 and 2 seconds
        wait_time = ((double)rand() / RAND_MAX) * 1.5 + 0.5;
        sleep(wait_time);

        receive_msg(conn_fd, conn_buffer, sizeof(conn_buffer));
        listening = 0;
    }

    send_msg(conn_fd);
    close(conn_fd);
}

//-- Server connection loop : accepts clients and handles each new connection
void
handle_connections()
{
    int conn_fd, conn_count = 0;
    struct sockaddr_in cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    pthread_t conn_threads[MAX_PARALLEL];
    
    while (1) {

        // Creates new threads while conn_count is below 100 (max specified)
        while (conn_count < MAX_PARALLEL) {

            // Accept a new client 
            conn_fd = accept(serv_sfd, (struct sockaddr*)&cliaddr, &cliaddr_len);
            if (conn_fd < 0) {
                perror("accept failed");
                continue;
            }

            conn_count++;   // update connection counter
            DEBUG_PRINTF("NEW CONNECTION ACCEPTED: %i\n", conn_fd);

            // Copy the connection fd to a pointer (malloc needed)
            int *conn_fd_ptr = malloc(sizeof(int));
            if (conn_fd_ptr == NULL) {
                perror("malloc failed");
                close(conn_fd);
                conn_count--;
                continue;
            }

            *conn_fd_ptr = conn_fd;

            // New thread to handle the accepted connection
            if (pthread_create(&conn_threads[conn_count - 1], NULL, 
                                (void*)connection_dialogue, (void*)conn_fd_ptr) != 0) {
                free(conn_fd_ptr);
                perror("pthread_create failed");
                close(conn_fd);
                continue;
            }

        }

        // When count reaches its max, wait for all threads with pthread join
        while (conn_count > 0) {
            pthread_join(conn_threads[conn_count - 1], NULL);
            conn_count--;
        }
    }
}

//-- Tries to get the server port and convert it to int
int
try_get_port(int argnum, char *str_port)
{
    char *endptr;
    long int li_port;

    if(argnum != 2) {
        fprintf(stderr, "usage: ./server <port>\n");
        exit(EXIT_FAILURE);
    }
    
    // Get long int from str and look for possible failures (bad format)
    li_port = strtol(str_port, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0') {
        fprintf(stderr, "error: non-valid port (bad format)\n");
        exit(EXIT_FAILURE);
    }
    return (int)li_port;
}

int
main(int argc, char *argv[]) 
{
    struct sockaddr_in servaddr;
    int port;

    port = try_get_port(argc, argv[1]);
    DEBUG_PRINTF("port is %i\n", port);

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // Create socket and set all proper configurations
    init_server_socket(&servaddr, port);

    bind_and_listen(&servaddr);

    // Signal managenent
    signal(SIGINT, handle_sigint);

    handle_connections();

    close(serv_sfd);

    exit(EXIT_SUCCESS);
}