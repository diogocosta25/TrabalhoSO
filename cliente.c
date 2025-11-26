#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

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

    snprintf(fifo_privado, sizeof(fifo_privado), "%s%d",CLIENTE_FIFO_BASE, getpid());

    if(mkfifo(fifo_privado, 0666) == -1){
        if (errno != EEXIST){
            perror("Erro ao criar FIFO privado do cliente");
            exit(1);
        }
    }
    printf("Cliente %d iniciado.FIFO: %s \n", getpid(),fifo_privado);


    p.pid_cliente=getpid();
   
    strncpy(p.username, argv[1], TAM_NOME);
    strncpy(p.comando, "login", TAM_COMANDOS);
    strncpy(p.args, "", TAM_ARGUMENTOS);

    int fd_controlador=open(CONTROLADOR_FIFO, O_WRONLY);
    if (fd_controlador == -1){
        perror("ERRO ao abrir controlador");
        unlink (fifo_privado);
        exit(1);
    }

    write(fd_controlador,&p,sizeof(Pedido));
    close(fd_controlador);

    int fd_privado=open(fifo_privado, O_RDONLY);
    read(fd_privado, &r,sizeof(Resposta));
    close(fd_privado);

    if (r.sucesso==0){
        perror("ERRO ao dar login");
        unlink(fifo_privado);
        exit(1);
    }

    printf("Login com sucesso\n");

    while (1){

        printf("->");
        fflush(stdout);

        if (fgets(linha_comando,sizeof(linha_comando),stdin)==NULL){
            break;
        }
        linha_comando[strcspn(linha_comando, "\n")]=0;
        

        p.pid_cliente=getpid();
        strncpy(p.username, argv[1],TAM_NOME);


        char *cmd=strtok(linha_comando," ");
        char *resto_args=strtok(NULL, "");

        if (cmd==NULL) continue;

        strncpy(p.comando, cmd, TAM_COMANDOS);

        if(resto_args!=NULL){
            strncpy(p.args, resto_args, TAM_ARGUMENTOS);
        }else{
            strcpy(p.args,"");
        }


        fd_controlador=open(CONTROLADOR_FIFO, O_WRONLY);

        if (fd_controlador==-1){
            perror("ERRO controlador desligou-se");
            break;
        }

        write(fd_controlador,&p,sizeof(Pedido));
        close(fd_controlador);

        fd_privado=open(fifo_privado, O_RDONLY);
        read(fd_privado, &r, sizeof(Resposta));
        close(fd_privado);

        printf("Server [%d]: %s\n", r.sucesso, r.mensagem);

        
    }

    printf("A terminar cliente\n");
    unlink(fifo_privado);

    return 0;
}