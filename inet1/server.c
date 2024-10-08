#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define SOCKET_CLOSED   0

#define F_FAILURE       -1
#define F_SUCCESS       0


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
init_server_socket(struct sockaddr_in *servaddr)
{
    const int ENABLE_SSOPT = 1;

    serv_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sfd < 0) {
        perror_exit("Error creating socket", SOCKET_CLOSED);
    }
    
    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);  // All interfaces
    servaddr->sin_port = htons(PORT);

    if (setsockopt(serv_sfd, SOL_SOCKET, SO_REUSEADDR, &ENABLE_SSOPT, sizeof(int)) < 0) {
        perror_exit_sr("setsockopt(SO_REUSEADDR) failed\n");
    }

    printf("Socket successfully created...\n");
    return F_SUCCESS;
}

//-- Bind and listen on the server socket
void
bind_and_listen(struct sockaddr_in *servaddr) 
{
    if (bind(serv_sfd, (struct sockaddr *) servaddr, sizeof(*servaddr)) < 0) {
        perror_exit_sr("bind failed");
    }
    printf("Socket successfully binded...\n");

    if (listen(serv_sfd, 1) < 0) {
        perror_exit_sr("listen failed");
    }
    printf("Server listening...\n");
}

//-- Blocks until a message is received
int
receive_msg(int conn_fd, char *buff, size_t buffsize) 
{
    DEBUG_PRINTF("[SERVER RECEIVING]\n");

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
send_msg(int conn_fd, char *buff, size_t buffsize)
{
    DEBUG_PRINTF("[SERVER SENDING]\n");

    // clear buffer and get the message from std input
    memset(buff, 0, buffsize);
    printf("> ");
    fgets(buff, buffsize, stdin);

    if (send(conn_fd, buff, strlen(buff), 0) < 0) {
        perror("send failed");
        return F_FAILURE;
    }
    return F_SUCCESS;
}

//-- Communication loop between client and server [this: server]
void
connection_dialogue(int conn_fd)
{
    char conn_buffer[1024];
    int talking = 1;

    while (talking) {
        
        if (receive_msg(conn_fd, conn_buffer, sizeof(conn_buffer)) < 0) {
            talking = 0;
            continue;
        }

        if (send_msg(conn_fd, conn_buffer, sizeof(conn_buffer)) < 0) {
            talking = 0;
        }
    }
}


int
main(int argc, char *argv[]) 
{
    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    int conn_fd;

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // Create socket and set all proper configurations
    init_server_socket(&servaddr);

    bind_and_listen(&servaddr);

    // Accept connection from client
    conn_fd = accept(serv_sfd, (struct sockaddr * ) &cliaddr, &cliaddr_len);
    if (conn_fd < 0) {
        perror_exit_sr("accept failed");
    }

    // Signal managenent
    signal(SIGINT, handle_sigint);

    // send and receive loop
    connection_dialogue(conn_fd);

    // if the function above ends, an error occurred
    close(conn_fd);
    close(serv_sfd);

    exit(EXIT_FAILURE);
}