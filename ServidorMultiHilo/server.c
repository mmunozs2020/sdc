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

#define RECV_DOAGAIN    2
#define RECV_BREAK      1
#define RECV_SUCCESS    0

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

//-- Prints an error message along the perror information and ends the execution
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

#define perror_exit_sr(msg) perror_exit(msg, SOCKET_RUNNING)

//-- Function to set up the server socket
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

//-- Wait with select() for the client file descriptor without "busy waiting"
int
wait_recv_timeout(int conn_fd, fd_set *readmask, struct timeval *timeout)
{
    DEBUG_PRINTF("Beggining of wait_recv_timeout() function...\n");

    int result = select(conn_fd + 1, readmask, NULL, NULL, timeout);

    DEBUG_PRINTF("Select clear, result returned: %i\n", result);

    if (result == -1) {
        perror("select() failed");
        return RECV_BREAK;
    }

    // There IS data to read from the file descriptor
    if (FD_ISSET(conn_fd, readmask)) {
        DEBUG_PRINTF(" `--> call recv()\n");
        return RECV_SUCCESS;
    }

    // Timeout: no data found
    DEBUG_PRINTF(" `--> continue;\n");
    return RECV_DOAGAIN;
}

//-- Returns a status after receiving n bytes (with n being: int bytes_received)
int
process_bytes_received(int bytes_received)
{
    if (bytes_received < 0) {
        perror("recv failed");
        return RECV_BREAK;
    }

    // In case the client receives a 0 byte msg, it means the server closed
    if (bytes_received == 0) {
        printf("Server closed the connection\n");
        return RECV_BREAK;
    }

    return RECV_SUCCESS;
}

//-- Communication loop between client and server [HERE: server]
void
connection_dialogue(int conn_fd)
{
    char input_buffer[1024], output_buffer[1024];
    int bytes_received, recv_status;
    int after_first_recv = 0;

    fd_set readmask;
    struct timeval timeout_base;        // timeout to set each iteration
    struct timeval timer;               // timer the wait_recv..() function uses

    timeout_base.tv_sec     = 15;           // seconds (15 s wait)
    timeout_base.tv_usec    = 0;            // microseconds

    while (SOCKET_RUNNING) {

        if(after_first_recv) {
            memset(output_buffer, 0, sizeof(output_buffer)); // clean output buffer

            // Wait for the user to enter a message to send to the client
            DEBUG_PRINTF("[server SENDING]\n");
            printf("> ");
            fgets(output_buffer, sizeof(output_buffer), stdin);

            // Send the message
            if (send(conn_fd, output_buffer, strlen(output_buffer), 0) < 0) {
                perror("send failed");
                break;
            }
        }

        // do always before wait_recv (!)
        FD_ZERO(&readmask);                 // Reset all deescriptors
        FD_SET(conn_fd, &readmask);         // Asign client descriptor

        DEBUG_PRINTF("[server RECEIVING with timeout %ld sec %ld micsec]\n",
                        timeout_base.tv_sec, timeout_base.tv_usec);
        
        // Set or reset the timer and call wait_recv..()
        timer = timeout_base;
        recv_status = wait_recv_timeout(conn_fd, &readmask, &timer);

        if (recv_status == RECV_DOAGAIN) {
            after_first_recv = 1;
            continue;
        }
        if (recv_status == RECV_BREAK) {
            break;
        }

        // Clean input buffer and receive the information from the server
        memset(input_buffer, 0, sizeof(input_buffer)); 
        bytes_received  = recv(conn_fd, input_buffer, sizeof(input_buffer), 
                                MSG_DONTWAIT);

        // Process the status of the recv() call and break loop if necessary
        recv_status     = process_bytes_received(bytes_received);
        if(recv_status == RECV_BREAK) {
            break;
        }

        // Print the message received with "+++ msg..." format
        input_buffer[bytes_received] = '\0';
        printf("+++ %s", input_buffer);
        after_first_recv = 1;
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

    // Signal managenent
    signal(SIGINT, handle_sigint);

    // Bind and listen
    if (bind(serv_sfd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0) {
        perror_exit_sr("bind failed");
    }
    printf("Socket successfully binded...\n");

    if (listen(serv_sfd, 1) < 0) {
        perror_exit_sr("listen failed");
    }
    printf("Server listening...\n");

    // Accept connection from client
    conn_fd = accept(serv_sfd, (struct sockaddr * ) &cliaddr, &cliaddr_len);
    if (conn_fd < 0) {
        perror_exit_sr("accept failed");
    }

    // Communication (loop) : starts receiving
    connection_dialogue(conn_fd);

    close(conn_fd);
    close(serv_sfd);

    DEBUG_PRINTF("Server file descriptors closed, terminating program\n");
    exit(EXIT_SUCCESS);
}