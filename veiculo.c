#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include "comum.h"

volatile sig_atomic_t continuar = 1;

void trata_sinal(int s) {
    (void)s;
    continuar = 0;
}

int main(int argc, char *argv[]){
    if(argc < 4){
        fprintf(stderr,"Erro argumentos insuficientes.\n");
        return 1;
    }

    int id_viagem = atoi(argv[1]);
    int pid_cliente = atoi(argv[2]);
    int distancia = atoi(argv[3]);

    signal(SIGUSR1, trata_sinal);

    char fifo_cliente[100];
    snprintf(fifo_cliente, sizeof(fifo_cliente), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);
    
    int fd_cli = open(fifo_cliente, O_WRONLY);
    if (fd_cli == -1) {
        perror("[VEICULO] Erro ao abrir FIFO do cliente");
        return 1;
    }

    Resposta r;
    r.sucesso = 1;
    snprintf(r.mensagem, TAM_MENSAGEM, "Veiculo para viagem %d chegou.", id_viagem);
    write(fd_cli, &r, sizeof(Resposta));
    
    int percorrido = 0;
    int ultimo_aviso_perc = 0;

    while(continuar && percorrido < distancia) {
        sleep(1);
        percorrido++;

        int percentagem = (percorrido * 100) / distancia;

        printf("VIAGEM %d %d%%\n", id_viagem, percentagem);
        fflush(stdout);

        if (percentagem >= ultimo_aviso_perc + 10 || percorrido == distancia) {
             snprintf(r.mensagem, TAM_MENSAGEM, "Viagem %d: %d%% concluida", id_viagem, percentagem);
             write(fd_cli, &r, sizeof(Resposta));
             ultimo_aviso_perc = percentagem - (percentagem % 10);
        }
    }

    if (!continuar) {
        printf("VIAGEM %d CANCELADA\n", id_viagem);
        // Avisar cliente que foi cancelado
        snprintf(r.mensagem, TAM_MENSAGEM, "Servico cancelado pelo controlador.");
        write(fd_cli, &r, sizeof(Resposta));
    } else {
        printf("VIAGEM %d CONCLUIDA\n", id_viagem);
        snprintf(r.mensagem, TAM_MENSAGEM, "Chegamos ao destino.");
        write(fd_cli, &r, sizeof(Resposta));
    }

    close(fd_cli);
    return 0;
}