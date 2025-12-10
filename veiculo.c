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

// Handler para quando o Controlador cancela a viagem (SIGUSR1)
void trata_sinal(int s) {
    (void)s;
    continuar = 0;
}

// Função auxiliar para enviar mensagens ao cliente
int envia_mensagem(int fd, Resposta *r) {
    if (write(fd, r, sizeof(Resposta)) == -1) {
        return 0; // O cliente desligou-se
    }
    return 1;
}

int main(int argc, char *argv[]) {
    // 1. Ignorar SIGPIPE para não crashar se o cliente fechar
    signal(SIGPIPE, SIG_IGN);

    // 2. Ler argumentos passados pelo Controlador (execl)
    if (argc < 4) {
        fprintf(stderr, "Erro: argumentos insuficientes.\n");
        return 1;
    }

    int id_viagem = atoi(argv[1]);
    int pid_cliente = atoi(argv[2]);
    int distancia = atoi(argv[3]); // A distância vem do 'agendar'

    // 3. Configurar sinais
    struct sigaction sa;
    sa.sa_handler = trata_sinal;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    // 4. Abrir o Pipe do Cliente (Só para escrever)
    char fifo_cliente[TAM_FIFO];
    snprintf(fifo_cliente, sizeof(fifo_cliente), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);

    int fd_cli = open(fifo_cliente, O_WRONLY);
    if (fd_cli == -1) {
        perror("[VEICULO] Erro ao abrir FIFO do cliente");
        return 1;
    }

    // 5. INÍCIO AUTOMÁTICO (Sem esperar por 'entrar')
    Resposta r;
    r.sucesso = 1;
    
    // Avisar o Controlador (stdout) e o Cliente (pipe) que chegámos
    printf("VIAGEM %d INICIADA (Auto-Start)\n", id_viagem);
    fflush(stdout); 
    
    snprintf(r.mensagem, TAM_MENSAGEM, "Veículo chegou! A iniciar viagem de %d Km...", distancia);
    strcpy(r.dados_extra, ""); 
    envia_mensagem(fd_cli, &r);

    // 6. SIMULAÇÃO DA VIAGEM
    int percorrido = 0;
    int ultimo_aviso_perc = 0;

    // Loop simples: 1 segundo = 1 Km (ou unidade de tempo)
    while (continuar && percorrido < distancia) {
        
        sleep(1); // O tempo passa...
        percorrido++;

        // Calcular percentagem
        int percentagem = (percorrido * 100) / distancia;

        // Enviar atualização a cada 10% ou no final
        if (percentagem >= ultimo_aviso_perc + 10 || percorrido == distancia) {
            
            // Relatório para o Controlador (Admin vê isto no comando 'frota')
            printf("VIAGEM %d %d%%\n", id_viagem, percentagem);
            fflush(stdout);

            // Relatório para o Cliente (Cliente vê isto no ecrã)
            snprintf(r.mensagem, TAM_MENSAGEM, "Progresso: %d%%", percentagem);
            if (!envia_mensagem(fd_cli, &r)) {
                // Cliente desapareceu, abortar
                printf("VIAGEM %d ABORTADA (Cliente saiu)\n", id_viagem);
                fflush(stdout);
                continuar = 0;
                break;
            }
            
            ultimo_aviso_perc = percentagem - (percentagem % 10);
        }
    }

    // 7. CONCLUSÃO
    if (continuar) {
        // Se saiu do loop porque chegou ao fim da distância
        printf("VIAGEM %d CONCLUIDA\n", id_viagem);
        fflush(stdout);

        snprintf(r.mensagem, TAM_MENSAGEM, "Chegámos ao destino! Viagem terminada.");
        envia_mensagem(fd_cli, &r);
    } 
    // Se saiu do loop porque continuar=0, o controlador já sabe (foi ele que cancelou)

    close(fd_cli);
    return 0;
}