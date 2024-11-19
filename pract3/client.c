#include "./stub.h"

// Función principal
int main(int argc, char **argv) {
    int exit_status = EXIT_SUCCESS;

    // Procesar los argumentos
    start_up_client(argc, argv);

    free_args();

    exit(exit_status);
}