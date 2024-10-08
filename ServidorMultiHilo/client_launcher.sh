#!/bin/bash

# Definimos las variables comunes
SERVER_IP="127.0.0.1"
PORT="8080"

# Bucle para lanzar el cliente 10 veces
for i in {1..231}
do
    # Ejecutar el cliente con client_id, server_ip y puerto
    ./client $i $SERVER_IP $PORT &
    
    # Esperar un intervalo aleatorio entre 0.1 y 0.2 segundos
    sleep $(awk -v min=0.001 -v max=0.002 'BEGIN{srand(); print min+rand()*(max-min)}')
done
