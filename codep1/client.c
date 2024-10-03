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

#define PORT 8073

int cli_sfd;

void handle_sigint(int sig) {
    printf("\nSocket shutdown received (CTRL+C)...\n");
    close(cli_sfd); // Cierra el socket del servidor
    exit(0);
}

int
main(int argc, char *argv[])
{
    struct sockaddr_in servaddr;
    char buffer_in[1024], buffer_send[1024];

    setbuf(stdout, NULL);

    cli_sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cli_sfd < 0) {
        perror("Error creating socket");
    }
    printf("Socket successfully created...\n");

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);  // # INADDR_ANY is 0.0.0.0 (all interfaces)
    servaddr.sin_port = htons(PORT);

    if (connect(cli_sfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect error");
        exit(EXIT_FAILURE);
    }
    printf("connected to the server...\n");

    signal(SIGINT, handle_sigint);

    while(1) {

        memset(buffer_send, 0, sizeof(buffer_send));

        printf("> ");
        fgets(buffer_send, sizeof(buffer_send), stdin);

        if(send(cli_sfd, buffer_send, strlen(buffer_send), 0) < 0) {
            perror("send failed");
            break;
        }


        memset(buffer_in, 0, sizeof(buffer_in));

        int bytes_received = recv(cli_sfd, buffer_in, sizeof(buffer_in) - 1, 0);

        if(bytes_received < 0) {
            perror("recv failed");
            break;
        }

        buffer_in[bytes_received] = '\0';
        printf("+++ %s\n", buffer_in);

    }

}