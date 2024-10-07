#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf("DEBUG: "__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif

#define PORT 8001

int sock_fd;

void close_socket() {
    int ret = close(sock_fd);
    if (ret < 0) {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
    printf("\nSocket successfully closed...\n");
}

// This function handles errors from system calls
// It receives the return value from a function and a custom
// message that will be printed in case of failure 
void handle_error(int ret, const char *msg) {
    if (ret < 0) {
        fprintf(stderr, "Error %s: %s\n", msg, strerror(errno));
        // Checks if the socket has been created to close it
        if (sock_fd != -1) {
            close_socket(sock_fd);
        }
        exit(EXIT_FAILURE);
    }
}

void handle_exit(int num) {
    close_socket(sock_fd);
    exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr;
    char buff[256];
    char data[100];
    char *result;
    int ret;

    signal(SIGINT, handle_exit); 

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    handle_error(sock_fd, "creating socket");
    printf("Socket successfully created...\n");

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);         // Server's port number

    ret = connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    handle_error(ret, "connecting to server");
    printf("Connected to the server...\n");

    do {
        // Wait for input
        printf("> ");
        result = fgets(data, sizeof(data), stdin);
        // Send message to client
        ret = send(sock_fd, data, sizeof(data), 0);
        handle_error(ret, "sending message");

        // Receive message from server
        ret = recv(sock_fd, buff, sizeof(buff), 0);
        if (ret == 0) {
            // Connection to the server closed
            close_socket(sock_fd);
            printf("Connection terminated\n");
            exit(EXIT_SUCCESS);
        }
        handle_error(ret, "receiving message");
        // Show message
        printf("+++ %s", buff);

    } while (result != NULL);

    exit(EXIT_SUCCESS);
}
