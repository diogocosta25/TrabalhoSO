#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include "comum.h"

int main(int argc, char *argv[]){
    char fifo_privado[100];
    char linha_comando[100];
    Pedido p;
    Resposta r;
    
    if(argc != 2){
        printf("Utilizacao : ./cliente <username>\n");
        exit(1);
    }

    snprintf(fifo_privado, sizeof(fifo_privado), "%s%d", CLIENTE_FIFO_BASE, getpid());

    if(mkfifo(fifo_privado, 0666) == -1){
        if (errno != EEXIST){
            perror("Erro ao criar FIFO privado do cliente");
            exit(1);
        }
    }
    printf("Cliente %d iniciado. FIFO: %s \n", getpid(), fifo_privado);

    p.pid_cliente = getpid();
    strncpy(p.username, argv[1], TAM_NOME);
    strncpy(p.comando, "login", TAM_COMANDOS);
    strncpy(p.args, "", TAM_ARGUMENTOS);

    int fd_controlador = open(CONTROLADOR_FIFO, O_WRONLY);
    if (fd_controlador == -1){
        perror("ERRO ao abrir controlador");
        unlink(fifo_privado);
        exit(1);
    }

    write(fd_controlador, &p, sizeof(Pedido));
    close(fd_controlador);

    int fd_privado = open(fifo_privado, O_RDONLY);
    
    ssize_t n_login = read(fd_privado, &r, sizeof(Resposta));
    close(fd_privado);

    if (n_login != sizeof(Resposta) || r.sucesso == 0){
        printf("ERRO no login: %s\n", r.mensagem);
        unlink(fifo_privado);
        exit(1);
    }

    printf("Login com sucesso\n");

    fd_set read_fds;
    int max_fd;

    fd_privado = open(fifo_privado, O_RDWR);  
    
    if (fd_privado == -1) { 
        perror("Erro ao abrir FIFO privado"); 
        exit(1); 
    }

    while (1){
        printf("->");
        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(fd_privado, &read_fds);

        max_fd = fd_privado > STDIN_FILENO ? fd_privado : STDIN_FILENO;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("Erro no select");
            break;
        }

        if (FD_ISSET(fd_privado, &read_fds)) {
            Resposta r_recebida;
            ssize_t n = read(fd_privado, &r_recebida, sizeof(Resposta));
            
            if (n == sizeof(Resposta)) {
                printf("\r[MENSAGEM]: %s\n", r_recebida.mensagem);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(linha_comando, sizeof(linha_comando), stdin) == NULL){
                break;
            }
            linha_comando[strcspn(linha_comando, "\n")] = 0;

            p.pid_cliente = getpid();
            strncpy(p.username, argv[1], TAM_NOME);

            char *cmd = strtok(linha_comando, " ");
            char *resto_args = strtok(NULL, "");

            if (cmd == NULL) continue;

            strncpy(p.comando, cmd, TAM_COMANDOS);

            if(resto_args != NULL){
                strncpy(p.args, resto_args, TAM_ARGUMENTOS);
            } else {
                strcpy(p.args, "");
            }

            fd_controlador = open(CONTROLADOR_FIFO, O_WRONLY);
            if (fd_controlador == -1){
                perror("ERRO controlador desligou-se");
                break;
            }

            write(fd_controlador, &p, sizeof(Pedido));
            close(fd_controlador);
        }
    }

    printf("A terminar cliente\n");
    close(fd_privado);
    unlink(fifo_privado);

    return 0;
}