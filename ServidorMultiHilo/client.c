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

// Socket file descriptor for client
int cli_sfd;

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
    servaddr->sin_addr.s_addr = htonl(INADDR_ANY);  // INADDR_ANY = 0.0.0.0
    servaddr->sin_port = htons(PORT);

    return cli_sfd;
}

//-- Wait with select() for the client file descriptor without "busy waiting"
int
wait_recv_timeout(fd_set *readmask, struct timeval *timeout)
{
    int result = select(cli_sfd + 1, readmask, NULL, NULL, timeout);

    DEBUG_PRINTF("Select clear, result returned: %i\n", result);

    if (result == -1) {
        perror("select() failed");
        return RECV_BREAK;
    }

    // There IS data to read from the file descriptor
    if (FD_ISSET(cli_sfd, readmask)) {
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

//-- Communication loop between client and server [HERE: client]
void
connection_dialogue(void)
{
    char input_buffer[1024], output_buffer[1024];
    int bytes_received, recv_status;

    fd_set readmask;
    struct timeval timeout_base;        // timeout to set each iteration
    struct timeval timer;               // timer the wait_recv..() function uses

    timeout_base.tv_sec     = 0;             // seconds
    timeout_base.tv_usec    = 500000;       // microseconds (0.5s = 500000)

    while (SOCKET_RUNNING) {
        
        memset(output_buffer, 0, sizeof(output_buffer)); // clean output buffer

        // Wait for the user to enter a message to send to the server
        DEBUG_PRINTF("[client SENDING]\n");
        printf("> ");
        fgets(output_buffer, sizeof(output_buffer), stdin);

        // Send the message
        if (send(cli_sfd, output_buffer, strlen(output_buffer), 0) < 0) {
            perror("send failed");
            break;
        }

        // do always before wait_recv (!)
        FD_ZERO(&readmask);             // Reset all deescriptors
        FD_SET(cli_sfd, &readmask);     // Asign client descriptor

        DEBUG_PRINTF("[client RECEIVING with timeout %ld sec %ld micsec]\n",
                        timeout_base.tv_sec, timeout_base.tv_usec);
                        
        // Set or reset the timer and call wait_recv..()
        timer = timeout_base;
        recv_status = wait_recv_timeout(&readmask, &timer);

        if (recv_status == RECV_DOAGAIN) {
            continue;
        }
        if (recv_status == RECV_BREAK) {
            break;
        }

        // Clean input buffer and receive the information from the server
        memset(input_buffer, 0, sizeof(input_buffer));
        bytes_received  = recv(cli_sfd, input_buffer, sizeof(input_buffer), 
                                MSG_DONTWAIT);

        // Process the status of the recv() call and break loop if necessary
        recv_status     = process_bytes_received(bytes_received);
        if(recv_status == RECV_BREAK) {
            break;
        }

        // Print the message received with "+++ msg..." format
        DEBUG_PRINTF("---- DATA RECEIVED FROM SERVER ----\n");
        input_buffer[bytes_received] = '\0';
        printf("+++ %s", input_buffer);
        DEBUG_PRINTF("----------- END OF DATA -----------\n");
    }
}

int
main(int argc, char *argv[])
{
    struct sockaddr_in servaddr;

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
    connection_dialogue();

    close(cli_sfd);

    DEBUG_PRINTF("Client file descriptor closed, terminating program\n");
    exit(EXIT_SUCCESS);

}