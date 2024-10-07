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

int server_fd;

void close_socket() {
    int ret = close(server_fd);
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
        if (server_fd != -1) {
            close_socket(server_fd);
        }
        exit(EXIT_FAILURE);
    }
}
 
void init_server_socket(struct sockaddr_in addr) {
    const int enable = 1;
    int ret;
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    handle_error(server_fd, "creating socket");
    printf("Socket successfully created...\n");

    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    handle_error(ret, "reusing socket address");

    // Establish server address
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all interfaces
    addr.sin_port = htons(PORT);        // Port in network byte order

    // Assign address to socket 
    ret = bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    handle_error(ret, "binding socket");
    printf("Socket successfully binded...\n");

    // Set socket in listening
    ret = listen(server_fd, 1);
    handle_error(ret, "listening state");
    printf("Server listening...\n");
}

void handle_exit(int num) {
    close_socket(server_fd);
    exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[]) {
    struct sockaddr_in serv_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd, ret;
    char buff[256];
    char data[256];
    char *result;

    signal(SIGINT, handle_exit);

    init_server_socket(serv_addr);
    // Accept client socket
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    handle_error(client_fd, "conecting to client");

    do {
        // Receive message from client
        ret = recv(client_fd, buff, sizeof(buff), 0);
        handle_error(ret, "receiving message");
        // Show message
        printf("+++ %s", buff);

        // Wait for input
        printf("> ");
        result = fgets(data, sizeof(data), stdin);
        // Send message to client
        ret = send(client_fd, data, sizeof(data), 0);
        handle_error(ret, "sending message");

    } while (result != NULL);

    exit(EXIT_SUCCESS);
}
