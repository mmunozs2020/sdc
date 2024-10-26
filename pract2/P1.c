#include "./stub.h"

int l_clock_loc = 0;

int
main(int argc, char *argv[])
{
    start_up_client(argc, argv);

    send_msg(cli_sfd, test_msg_s);

    struct message *test_msg_r = create_empty_msg();
    receive_msg(cli_sfd, test_msg_r);
    update_clock_lamport(&l_clock_loc);

    print_msg(test_msg_r);

    // print PX, contador_lamport, SEND, operations

    close(cli_sfd);

    DEBUG_PRINTF("Client file descriptor closed, terminating program\n");
    exit(EXIT_SUCCESS);

}