#include "./stub.h"

int l_clock_loc = 0;

int
main(int argc, char *argv[])
{
    char *whoami = "P2";

    start_up_server(argc, argv, whoami);
    DEBUG_PRINTF("\n\n>\n>>P2>>: server started up correctly\n>\n\n");

    while (get_clock_lamport() != 3) {
        continue;
    }
    DEBUG_PRINTF("\n\n>\n>>P2>>: server received 1 message\n>\n\n");

    send_msg(whoami, "P1", SHUTDOWN_NOW);
    DEBUG_PRINTF("\n\n>\n>>P2>>: server sent SHUTDOWN_NOW to P1\n>\n\n");

    while (get_clock_lamport() != 7) {
        continue;
    }
    DEBUG_PRINTF("\n\n>\n>>P2>>: server received 1 message\n>\n\n");

    send_msg(whoami, "P3", SHUTDOWN_NOW);
    DEBUG_PRINTF("\n\n>\n>>P2>>: server sent SHUTDOWN_NOW to P3\n>\n\n");

    while (get_clock_lamport() != 11) {
        continue;
    }
    terminate_server(EXIT_SUCCESS);

}