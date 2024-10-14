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
#define SOCKET_CLOSED   0

#define F_FAILURE       -1
#define F_SUCCESS       0

// WR stands for wait_receive (first words of a function below)
#define WR_SUCCESS      0
#define WR_FAILURE      -1
#define WR_NTR          -2  // NTR stands for Nothing To Read


// Socket file descriptor for client
int cli_sfd;

// Socket file descriptor for server
int serv_sfd;


//-- Handles SIGINT signals so the CLIENT can be stopped with CTRL+C
void 
handle_sigint(int sig)
{
    printf("\nSocket shutdown received (CTRL+C)...\n");
    close(cli_sfd); // Close server socket
    
    exit(EXIT_SUCCESS);
}

//-- Prints an error message along the perror information and ends the execution
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

//-- Function to set up the server socket
int 
init_client_socket(struct sockaddr_in *servaddr)
{
    cli_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_sfd < 0) {
        perror_exit("Error creating socket", SOCKET_CLOSED);
    }
    printf("Socket successfully created...\n");

    servaddr->sin_family = AF_INET;
    if (inet_pton(AF_INET, "127.0.0.1", &(servaddr->sin_addr)) <= 0) {
        perror("Invalid address / Address not supported");
        exit(EXIT_FAILURE);
    }
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

//-- Wait with select() for the client file descriptor without "busy waiting" : FUSE WITH CONN_DIALOGUE
int
wait_recv_timeout(fd_set *readmask, struct timeval *timeout)
{
    int result = select(cli_sfd + 1, readmask, NULL, NULL, timeout);

    DEBUG_PRINTF("Select clear, result returned: %i\n", result);

    if (result == -1) {
        perror("select() failed");
        return WR_FAILURE;
    }

    // There IS data to read from the file descriptor
    if (FD_ISSET(cli_sfd, readmask)) {
        DEBUG_PRINTF(" `--> call recv()\n");
        return WR_SUCCESS;
    }

    // Timeout: no data found
    DEBUG_PRINTF(" `--> continue;\n");
    return WR_NTR;
}

//-- Communication loop between client and server [HERE: client] -> DO NOT USE, ONLY REFERENCE
int
connection_dialogue(void)
{
    char conn_buffer[1024];
    int wait_recv_status, recv_status, exit_status = EXIT_FAILURE, talking = 1;

    fd_set readmask;
    struct timeval timeout_base;        // timeout to set each iteration
    struct timeval timer;               // timer the wait_recv..() function uses

    timeout_base.tv_sec     = 0;             // seconds
    timeout_base.tv_usec    = 500000;       // microseconds (0.5s = 500000)

    while (talking) {
        
        send_msg(cli_sfd, conn_buffer, sizeof(conn_buffer));

        // do always before wait_recv (!)
        FD_ZERO(&readmask);             // Reset all deescriptors
        FD_SET(cli_sfd, &readmask);     // Asign client descriptor
        timer = timeout_base;

        wait_recv_status = wait_recv_timeout(&readmask, &timer);
        if (wait_recv_status < 0) {
            // If wait_recv_...() returned with FAILURE, end the talking loop
            if (wait_recv_status == WR_FAILURE) {
                talking = 0;
            }
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



//* ------------------------------------------------- */




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
    int bytes_received = recv(conn_fd, buff, buffsize, MSG_DONTWAIT);
    
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

//-- Wait with select() for the client file descriptor without "busy waiting"
int
wait_recv_timeout(int conn_fd, fd_set *readmask, struct timeval *timeout)
{
    int select_result;

    DEBUG_PRINTF("Beggining of wait_recv_timeout() function...\n");
    select_result = select(conn_fd + 1, readmask, NULL, NULL, timeout);

    if (select_result == F_FAILURE) {
        perror("select() failed");
        return WR_FAILURE;
    }

    // There IS data to read from the file descriptor
    if (FD_ISSET(conn_fd, readmask)) {
        DEBUG_PRINTF(" `--> call recv()\n");
        return WR_SUCCESS;
    }

    // Timeout: no data found
    DEBUG_PRINTF(" `--> continue;\n");
    return WR_NTR;
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

//-- Communication loop between client and server [HERE: server]
void
connection_dialogue(int conn_fd)
{
    char conn_buffer[1024];
    int wait_recv_status, can_send = 0, talking = 1;

    fd_set readmask;
    struct timeval timeout_base;        // timeout to set each iteration
    struct timeval timer;               // timer the wait_recv..() function uses

    timeout_base.tv_sec     = 15;           // seconds (15 s wait)
    timeout_base.tv_usec    = 0;            // microseconds

    while (talking) {

        // First time, can_send is always 0 so the server starts receiving
        if(can_send) {
            if(send_msg(conn_fd, conn_buffer, sizeof(conn_buffer)) < 0) {
                talking = 0;
                continue;
            }
        }

        // do always before wait_recv (!)
        FD_ZERO(&readmask);                 // Reset all deescriptors
        FD_SET(conn_fd, &readmask);         // Asign client descriptor
        timer = timeout_base;

        wait_recv_status = wait_recv_timeout(conn_fd, &readmask, &timer);
        if (wait_recv_status < 0) {
            can_send = 1;
            // If wait_recv_...() returned with FAILURE, end the talking loop
            if (wait_recv_status == WR_FAILURE) {
                talking = 0;
            }
            continue;
        }
        
        if (receive_msg(conn_fd, conn_buffer, sizeof(conn_buffer)) < 0) {
            talking = 0;
        }

        can_send = 1;
    }
}

