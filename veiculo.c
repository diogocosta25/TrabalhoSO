#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "comum.h"

int main(int argc, char *argv[]){
    if(argc < 4){
        fprintf(stderr,"Erro argumentos insuficientes.\n");
        return 1;
    }

    int id_viagem = atoi (argv[1]);
    int pid_cliente = atoi(argv[2]);
    int distancia = atoi(argv[3]);

    printf("Veiculo em viagem %d (Distancia: %d Km)\n",id_viagem, distancia);

    char fifo_cliente[100];

}