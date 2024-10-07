#include <stdio.h>
#include <unistd.h>  // Para usar sleep()

int main() {
    setbuf(stdout, NULL);  // Desactiva el buffering para stdout

    printf("Contando:\n");
    for (int i = 1; i <= 5; i++) {
        printf("%d ", i);
        sleep(1);  // Espera de 1 segundo
    }

    printf("\n");  // Salto de línea final
    return 0;
}

