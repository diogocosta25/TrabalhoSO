#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include "comum.h"

int main(int argc, char *argv[]){
    char fifo_privado[TAM_FIFO];
    char linha_comando[100];
    char fifo_veiculo_atual[TAM_FIFO] = "";
    Pedido p;
    Resposta r;
    
    if(argc != 2){
        printf("Utilizacao : ./cliente <username>\n");
        exit(1);
    }

    snprintf(fifo_privado, sizeof(fifo_privado), "%s%d", CLIENTE_FIFO_BASE, getpid());

    if(mkfifo(fifo_privado, 0666) == -1 && errno != EEXIST){
        perror("Erro ao criar FIFO privado");
        exit(1);
    }

    printf("Cliente %s (%d) iniciado.\n", argv[1], getpid());

    // login
    p.pid_cliente = getpid();
    strncpy(p.username, argv[1], TAM_NOME);
    strncpy(p.comando, "login", TAM_COMANDOS);
    strncpy(p.args, "", TAM_ARGUMENTOS);

    int fd_controlador = open(CONTROLADOR_FIFO, O_WRONLY);
    if (fd_controlador == -1){
        perror("ERRO: Controlador nao acessivel");
        unlink(fifo_privado);
        exit(1);
    }
    write(fd_controlador, &p, sizeof(Pedido));
    close(fd_controlador);

    int fd_privado = open(fifo_privado, O_RDWR);
    if (fd_privado == -1) { perror("Erro FIFO privado"); exit(1); }
    
    ssize_t n = read(fd_privado, &r, sizeof(Resposta));
    if (n != sizeof(Resposta) || r.sucesso == 0){
        printf("ERRO no login: %s\n", r.mensagem);
        close(fd_privado); unlink(fifo_privado);
        exit(1);
    }
    printf("%s", r.mensagem);

    fd_set read_fds;
    int max_fd;

    while (1){
        printf(">> ");
        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(fd_privado, &read_fds);

        max_fd = fd_privado > STDIN_FILENO ? fd_privado : STDIN_FILENO;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) break;

        // mensagens recebidas
        if (FD_ISSET(fd_privado, &read_fds)) {
            Resposta r_rec;
            ssize_t n = read(fd_privado, &r_rec, sizeof(Resposta));
            
            if (n == sizeof(Resposta)) {
                if (strlen(r_rec.dados_extra) > 0 && strstr(r_rec.dados_extra, VEICULO_FIFO_BASE)) {
                    strncpy(fifo_veiculo_atual, r_rec.dados_extra, TAM_FIFO);
                    printf("\r[VEICULO]: %s (Pode entrar agora)\n", r_rec.mensagem);
                } else {
                    printf("\r[INFO]: %s\n", r_rec.mensagem);
                }
                
                // viagem acabou ou foi cancelada, limpar fifo do veiculo
                if (strstr(r_rec.mensagem, "Chegamos") || strstr(r_rec.mensagem, "cancelado") || strstr(r_rec.mensagem, "interrompida")) {
                    strcpy(fifo_veiculo_atual, "");
                }
            }
        }

        // comandos utilizador
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(linha_comando, sizeof(linha_comando), stdin) == NULL) break;
            linha_comando[strcspn(linha_comando, "\n")] = 0;

            char *cmd = strtok(linha_comando, " ");
            char *arg = strtok(NULL, "");

            if (!cmd) continue;

            if (strcmp(cmd, "sair") == 0 || strcmp(cmd, "entrar") == 0) {
                // comandos para o veiculo
                if (strlen(fifo_veiculo_atual) == 0) {
                    printf("Erro: Nenhum veiculo aguardando ou em viagem.\n");
                    continue;
                }

                int fd_veic = open(fifo_veiculo_atual, O_WRONLY);
                if (fd_veic == -1) {
                    perror("Erro ao comunicar com veiculo");
                    strcpy(fifo_veiculo_atual, "");
                    continue;
                }

                PedidoVeiculo pv;
                if (strcmp(cmd, "entrar") == 0) {
                    if (arg == NULL) { printf("Uso: entrar <destino>\n"); close(fd_veic); continue; }
                    pv.codigo = 1;
                    strncpy(pv.destino, arg, TAM_ARGUMENTOS);
                } else { // sair
                    pv.codigo = 2;
                    strcpy(pv.destino, "");
                }

                write(fd_veic, &pv, sizeof(PedidoVeiculo));
                close(fd_veic);

            } else if (strcmp(cmd, "terminar") == 0) {
                break;
            } else {
                // comandos para o controlador
                p.pid_cliente = getpid();
                strncpy(p.username, argv[1], TAM_NOME);
                strncpy(p.comando, cmd, TAM_COMANDOS);
                if(arg) strncpy(p.args, arg, TAM_ARGUMENTOS);
                else strcpy(p.args, "");

                fd_controlador = open(CONTROLADOR_FIFO, O_WRONLY);
                if (fd_controlador != -1){
                    write(fd_controlador, &p, sizeof(Pedido));
                    close(fd_controlador);
                } else {
                    printf("Controlador offline.\n");
                    break;
                }
            }
        }
    }

    close(fd_privado);
    unlink(fifo_privado);
    return 0;
}