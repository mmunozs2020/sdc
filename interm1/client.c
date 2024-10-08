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


// Socket file descriptor for client
int cli_sfd;


//-- Handles SIGINT signals so the CLIENT can be stopped with CTRL+C
void 
handle_sigint(int sig)
{
    printf("\nSocket shutdown received (CTRL+C)...\n");
    close(cli_sfd); // Close client socket
    
    exit(EXIT_SUCCESS);
}

//-- Prints the proper error msg and terminates with failure
void 
perror_exit(char *msg, int socket_running)
{
    perror(msg);

    if (socket_running) {
        DEBUG_PRINTF("Closing clilent socket after perror\n");
        close(cli_sfd);
    }

    exit(EXIT_FAILURE);
}

//-- Calls perror_exit with SOCKET_RUNNING as second parameter
#define perror_exit_sr(msg) perror_exit(msg, SOCKET_RUNNING)

//-- Sets up the client socket configuration
int 
init_client_socket(struct sockaddr_in *servaddr)
{
    cli_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_sfd < 0) {
        perror_exit("Error creating socket", SOCKET_CLOSED);
    }
    printf("Socket successfully created...\n");

    servaddr->sin_family = AF_INET;
    servaddr->sin_addr.s_addr = htonl("212.128.255.24");  // # INADDR_ANY is 0.0.0.0 (all interfaces)
    servaddr->sin_port = htons(PORT);

    return cli_sfd;
}

//-- Blocks until a message is received
int
receive_msg(int conn_fd, char *buff, size_t buffsize) 
{
    DEBUG_PRINTF("[CLIENT RECEIVING]\n");

    // clear buffer and block until receiving a msg
    memset(buff, 0, buffsize);
    int bytes_received = recv(conn_fd, buff, buffsize, 0);
    
    if (bytes_received < 0) {
        perror("recv failed");
        return F_FAILURE;
    }

    // If 0-byte msg received it means the server closed the connection
    if (bytes_received == 0) {
        printf("Server closed the connection\n");
        return F_SUCCESS;
    }
    
    // null-terminate the msg and print it after the "+++" indicator
    buff[bytes_received] = '\0';
    printf("+++ %s", buff);
    return bytes_received;
}

//-- Sends a message to the server
int
send_msg(int conn_fd, char *buff, size_t buffsize)
{
    DEBUG_PRINTF("[CLIENT SENDING]\n");

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

//-- Communication loop between client and server [this: client]
int
connection_dialogue(void)
{
    char conn_buffer[1024];
    int recv_status, exit_status = EXIT_FAILURE, talking = 1;

    while (talking) {

        if (send_msg(cli_sfd, conn_buffer, sizeof(conn_buffer)) < 0) {
            talking = 0;
            continue;
        }

        recv_status = receive_msg(cli_sfd, conn_buffer, sizeof(conn_buffer));
        if (recv_status <= 0) {
            // If receive_msg returned F_SUCCESS, the dialogue ends with SUCCESS
            if(recv_status == F_SUCCESS) {
                exit_status = EXIT_SUCCESS;
            }
            talking = 0;
        }
    }
    return exit_status;
}

int
main(int argc, char *argv[])
{
    struct sockaddr_in servaddr;
    int conn_exit_status;

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // Create socket and set all proper configurations
    init_client_socket(&servaddr);

    // Signal management
    signal(SIGINT, handle_sigint);

    // Connect to server
    if (connect(cli_sfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror_exit_sr("connect error");
    }
    printf("connected to the server...\n");

    // Communication (loop) : starts sending
    conn_exit_status = connection_dialogue();

    close(cli_sfd);

    DEBUG_PRINTF("Client file descriptor closed, terminating program\n");
    exit(conn_exit_status);

}