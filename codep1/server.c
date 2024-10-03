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

//------------------------------------------------------------------------------

#define PORT 8073

#define handle_error(msg) \
do { perror(msg); exit(EXIT_FAILURE); } while (0)

int serv_sfd;

void handle_sigint(int sig) {
    printf("\nSocket shutdown received (CTRL+C)...\n");
    close(serv_sfd); // Cierra el socket del servidor
    exit(0);
}

int
main(int argc, char *argv[])
{
    const int ENABLE_SSOPT = 1;

    struct sockaddr_in servaddr, cliaddr;
    socklen_t cliaddr_len = sizeof(cliaddr);
    char input_buffer[1024], output_buffer[1024];
    int conn_fd, bytes_received;

    setbuf(stdout, NULL);

    serv_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if(serv_sfd < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);  // # INADDR_ANY is 0.0.0.0 (all interfaces)
    servaddr.sin_port = htons(PORT);

    if( setsockopt(serv_sfd, SOL_SOCKET, SO_REUSEADDR, &ENABLE_SSOPT, sizeof(int)) < 0) {
        handle_error("setsockopt(SO_REUSEADDR) failed\n");
    }
    printf("Socket successfully created...\n");

    if( bind(serv_sfd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully binded...\n");

    if( listen(serv_sfd, 1) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    printf("Server listening...\n");

    if( (conn_fd = accept(serv_sfd, (struct sockaddr * ) &cliaddr, &cliaddr_len)) < 0) {
        perror("accept failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_sigint);

    while(1) {

        memset(input_buffer, 0, sizeof(input_buffer));

        printf("> ");
        bytes_received = recv(conn_fd, input_buffer, sizeof(input_buffer), 0);

        if(bytes_received < 0) {
            perror("recv failed");
            break;
        }
        
        input_buffer[bytes_received] = '\0';
        printf("+++ %s\n", input_buffer);

        memset(output_buffer, 0, sizeof(output_buffer));

        fgets(output_buffer, sizeof(output_buffer), stdin);

        if(send(conn_fd, output_buffer, strlen(output_buffer), 0) < 0) {
            perror("send failed");
            break;
        }

    }

    close(conn_fd);
    close(serv_sfd);
}