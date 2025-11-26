#ifndef COMUM_H
#define COMUM_H

#include <unistd.h>

#define CONTROLADOR_FIFO "/tmp/so_controlador_fifo"
#define CLIENTE_FIFO_BASE "/tmp/so_cli_"

#define TAM_NOME 50
#define TAM_COMANDOS 20
#define TAM_ARGUMENTOS 100
#define TAM_MENSAGEM 100


typedef struct {
    pid_t pid_cliente;
    char username[TAM_NOME];
    char comando[TAM_COMANDOS];
    char args [TAM_ARGUMENTOS];
} Pedido;


typedef struct{
    int sucesso;
    char mensagem[TAM_MENSAGEM];
}Resposta;




#endif