#include "./stub.h"

int l_clock_loc = 0;

int
main(int argc, char *argv[])
{
    char *whoami = "P1";

    start_up_client(argc, argv, whoami);
    DEBUG_PRINTF("\n\n>\n>>P1>>: client started up correctly\n>\n\n");

    send_msg(whoami, "P2", READY_TO_SHUTDOWN);
    DEBUG_PRINTF("\n\n>\n>>P1>>: sent READY_TO_SHUTDOWN\n>\n\n");

    while (get_clock_lamport() != 5) {
        continue;
    }
    DEBUG_PRINTF("\n\n>\n>>P1>>: client received 1 message\n>\n\n");

    send_msg(whoami, "P2", SHUTDOWN_ACK);
    DEBUG_PRINTF("\n\n>\n>>P1>>: client sent SHUTDOWN_ACK\n>\n\n");

    terminate_client(EXIT_SUCCESS);
}