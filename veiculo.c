#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <errno.h>
#include "comum.h"

volatile sig_atomic_t continuar = 1;

void trata_sinal(int s) {
    (void)s;
    continuar = 0;
}

int envia_mensagem(int fd, Resposta *r) {
    if (write(fd, r, sizeof(Resposta)) == -1) {
        return 0; // cliente morreu
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

    char fifo_veiculo[TAM_FIFO];
    snprintf(fifo_veiculo, sizeof(fifo_veiculo), "%s%d", VEICULO_FIFO_BASE, getpid());

    if (mkfifo(fifo_veiculo, 0666) == -1) {
        if (errno != EEXIST) {
            perror("[VEICULO] Erro ao criar FIFO privado");
            return 1;
        }
    }

    char fifo_cliente[TAM_FIFO];
    snprintf(fifo_cliente, sizeof(fifo_cliente), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);

    int fd_cli = open(fifo_cliente, O_WRONLY);
    if (fd_cli == -1) {
        perror("[VEICULO] Erro ao abrir FIFO do cliente");
        unlink(fifo_veiculo);
        return 1;
    }

    Resposta r;
    r.sucesso = 1;
    snprintf(r.mensagem, TAM_MENSAGEM, "Veiculo chegou! Use 'entrar <destino>'");
    strncpy(r.dados_extra, fifo_veiculo, TAM_ARGUMENTOS);
    write(fd_cli, &r, sizeof(Resposta));
    
    int fd_veiculo = open(fifo_veiculo, O_RDWR);
    if (fd_veiculo == -1) {
        perror("[VEICULO] Erro ao abrir meu FIFO");
        close(fd_cli);
        unlink(fifo_veiculo);
        return 1;
    }

    PedidoVeiculo pv;
    printf("VIAGEM %d AGUARDANDO\n", id_viagem);
    fflush(stdout);

    int cliente_entrou = 0;
    
    while (continuar && !cliente_entrou) {
        ssize_t n = read(fd_veiculo, &pv, sizeof(PedidoVeiculo));
        if (n == sizeof(PedidoVeiculo)) {
            if (pv.codigo == 1) {
                cliente_entrou = 1;
                printf("VIAGEM %d INICIADA DESTINO %s\n", id_viagem, pv.destino);
                fflush(stdout);
                
                snprintf(r.mensagem, TAM_MENSAGEM, "Bem-vindo a bordo. Destino: %.60s", pv.destino);
                strcpy(r.dados_extra, "");
                write(fd_cli, &r, sizeof(Resposta));
            }
        } else if (n < 0 && errno != EINTR) {
            break; 
        }
    }

    int percorrido = 0;
    int ultimo_aviso_perc = 0;
    
    fd_set read_fds;
    struct timeval tv;

    while (continuar && cliente_entrou && percorrido < distancia) {
        FD_ZERO(&read_fds);
        FD_SET(fd_veiculo, &read_fds);
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(fd_veiculo + 1, &read_fds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(fd_veiculo, &read_fds)) {
            ssize_t n = read(fd_veiculo, &pv, sizeof(PedidoVeiculo));
            if (n == sizeof(PedidoVeiculo) && pv.codigo == 2) { // SAIR
                printf("VIAGEM %d CANCELADA PELO CLIENTE\n", id_viagem);
                fflush(stdout);
                snprintf(r.mensagem, TAM_MENSAGEM, "Viagem interrompida.");
                write(fd_cli, &r, sizeof(Resposta));
                cliente_entrou = 0;
                break;
            }
        }

        if (cliente_entrou) {
            percorrido++;
            int percentagem = (percorrido * 100) / distancia;

            if (percentagem >= ultimo_aviso_perc + 10 || percorrido == distancia) {
                printf("VIAGEM %d %d%%\n", id_viagem, percentagem);
                fflush(stdout);

                snprintf(r.mensagem, TAM_MENSAGEM, "Progresso: %d%%", percentagem);
                
                if (!envia_mensagem(fd_cli, &r)) {
                    printf("VIAGEM %d CANCELADA (CLIENTE PERDIDO)\n", id_viagem);
                    fflush(stdout);
                    continuar = 0;
                    break;
                }
                ultimo_aviso_perc = percentagem - (percentagem % 10);
            }
        }
    }

    if (!continuar) {
        if (percorrido < distancia) {
        }
    } else if (percorrido >= distancia) {
        printf("VIAGEM %d CONCLUIDA\n", id_viagem); 
        snprintf(r.mensagem, TAM_MENSAGEM, "Chegamos ao destino.");
        envia_mensagem(fd_cli, &r);
    }

        close(fd_cli);
        close(fd_veiculo);
        unlink(fifo_veiculo);
        return 0;
}