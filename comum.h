#ifndef COMUM_H
#define COMUM_H

#include <unistd.h>
#include <sys/types.h>

#define CONTROLADOR_FIFO "/tmp/so_controlador_fifo"
#define CLIENTE_FIFO_BASE "/tmp/so_cli_"
#define VEICULO_FIFO_BASE "/tmp/so_veic_"

#define TAM_NOME 50
#define TAM_COMANDOS 20
#define TAM_ARGUMENTOS 100
#define TAM_MENSAGEM 100
#define TAM_FIFO 100

typedef struct {
    pid_t pid_cliente;
    char username[TAM_NOME];
    char comando[TAM_COMANDOS];
    char args[TAM_ARGUMENTOS];
} Pedido;

typedef struct {
    int sucesso;
    char mensagem[TAM_MENSAGEM];
    char dados_extra[TAM_ARGUMENTOS];
} Resposta;

typedef struct {
    int codigo;
    char destino[TAM_ARGUMENTOS];
} PedidoVeiculo;

#endif