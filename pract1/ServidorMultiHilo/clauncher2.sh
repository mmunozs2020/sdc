#!/bin/bash

if [ "$#" -ne 2 ]; then
    echo "Uso: $0 <SERVER_IP> <PORT>"
    exit 1
fi

SERVER_IP="$1"
PORT="$2"

for i in {1..231}
do
    ./client $i $SERVER_IP $PORT &
    
    sleep $(awk -v min=0.001 -v max=0.01 'BEGIN{srand(); print min+rand()*(max-min)}')
done
