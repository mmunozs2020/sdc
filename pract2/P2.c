#include "./stub.h"

int l_clock_loc = 0;

int
main(int argc, char *argv[])
{
    int port, serv_sfd, conn_fd;
    struct sockaddr_in servaddr, cliaddr;
    char *server_ip;

    // Disable buffering when printing messages
    setbuf(stdout, NULL);

    // does several verifications while initializing the server
    // ->   [meter esto en una funcion que haga todo... ]
    //      [llamar a dicha funcion al comienzo de un thread de comunicacion]
    if (check_argnum(argc) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    port        = try_get_port(argv[2]);
    server_ip   = argv[1];
    if (port == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    serv_sfd = init_socket(&servaddr, server_ip, port);
    if (serv_sfd == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    enable_setsockopt(serv_sfd);

    if (bind_and_listen(serv_sfd, &servaddr) == F_FAILURE) {
        exit(EXIT_FAILURE);
    }

    conn_fd = accept_client(serv_sfd, &cliaddr);
    if (conn_fd == F_FAILURE) {
        exit(EXIT_FAILURE);
    }
    
    signal(SIGINT, handle_sigint);
    // ->   [ ...]
    //      [HASTA AQUI]

    
    struct message *test_msg_r = create_empty_msg();
    receive_msg(conn_fd, test_msg_r);
    update_clock_lamport(&l_clock_loc);

    update_clock_lamport(&l_clock_loc);
    struct message *test_msg_s = create_msg("P2", SHUTDOWN_NOW, l_clock_loc);
    send_msg(conn_fd, test_msg_s);

    print_msg(test_msg_r);

    // print PX, contador_lamport, SEND, operations

    close(serv_sfd);

    DEBUG_PRINTF("Client file descriptor closed, terminating program\n");
    exit(EXIT_SUCCESS);

}