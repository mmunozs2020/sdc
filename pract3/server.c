#include "./stub.h"

// Función principal
int main(int argc, char **argv) {
    int exit_status = EXIT_SUCCESS;

    start_up_server(argc, argv);

    free_args();

    exit(exit_status);
}