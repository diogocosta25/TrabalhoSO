#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "comum.h"

// Variável global para controlar se a viagem continua
volatile sig_atomic_t continuar = 1;

void trata_sinal(int s) {
    (void)s;
    continuar = 0;
}

int envia_mensagem(int fd, Resposta *r) {
    if (write(fd, r, sizeof(Resposta)) == -1) {
        return 0; 
    }
    return 1;
}

int main(int argc, char *argv[]) {
    signal(SIGPIPE, SIG_IGN);

    if (argc < 4) {
        fprintf(stderr, "Erro: argumentos insuficientes.\n");
        return 1;
    }

    int id_viagem = atoi(argv[1]);
    int pid_cliente = atoi(argv[2]);
    int distancia = atoi(argv[3]); 

    struct sigaction sa;
    sa.sa_handler = trata_sinal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    char fifo_cliente[TAM_FIFO];
    snprintf(fifo_cliente, sizeof(fifo_cliente), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);

    int fd_cli = open(fifo_cliente, O_WRONLY);
    if (fd_cli == -1) {
        perror("[VEICULO] Erro ao abrir FIFO do cliente");
        return 1;
    }

    Resposta r;
    r.sucesso = 1;
    
    printf("VIAGEM %d INICIADA (Auto-Start)\n", id_viagem);
    fflush(stdout); 
    
    snprintf(r.mensagem, TAM_MENSAGEM, "Veículo chegou! A iniciar viagem de %d Km...", distancia);
    strcpy(r.dados_extra, ""); 
    envia_mensagem(fd_cli, &r);

    int percorrido = 0;
    int ultimo_aviso_perc = 0;

    while (continuar && percorrido < distancia) {
        
        sleep(1); 
        percorrido++;

        int percentagem = (percorrido * 100) / distancia;

        if (percentagem >= ultimo_aviso_perc + 10 || percorrido == distancia) {
            
            printf("VIAGEM %d %d%%\n", id_viagem, percentagem);
            fflush(stdout);

            snprintf(r.mensagem, TAM_MENSAGEM, "Progresso: %d%%", percentagem);
            if (!envia_mensagem(fd_cli, &r)) {
                printf("VIAGEM %d ABORTADA (Cliente saiu)\n", id_viagem);
                fflush(stdout);
                continuar = 0;
                break;
            }
            
            ultimo_aviso_perc = percentagem - (percentagem % 10);
        }
    }

    if (continuar) {
        printf("VIAGEM %d CONCLUIDA\n", id_viagem);
        fflush(stdout);

        snprintf(r.mensagem, TAM_MENSAGEM, "Chegámos ao destino! Viagem terminada.");
        envia_mensagem(fd_cli, &r);
    } else{
        printf("VIAGEM %d CANCELADA\n", id_viagem);
        fflush(stdout);
    }

    close(fd_cli);
    return 0;
}