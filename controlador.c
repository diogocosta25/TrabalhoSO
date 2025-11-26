#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h> 
#include <sys/wait.h>

#include "comum.h" 

#define MAX_USERS 30
#define MAX_SERVICOS 30

typedef struct {
    int id;
    char cliente[TAM_NOME];
    char origem[TAM_ARGUMENTOS]; 
    int distancia; 
    int estado; // 0 = Agendado, 1 = Em Curso, 2 = Concluído
    int hora;
    int pidCliente;

} Viagem;

Viagem viagem[MAX_SERVICOS];
int nViagens=0;


char utilizadores[MAX_USERS][TAM_NOME];
int n_users = 0;

int maxVeiculos=0;
int veiculosCirculacao=0;
int tempoDecorrido=0;


pthread_mutex_t trinco = PTHREAD_MUTEX_INITIALIZER;

void *monitorVeiculo(void *arg){
    int fd_pipe= *(int*)arg;
    free(arg);

    char buffer[128];
    int n;

    while((n=read(fd_pipe, buffer, sizeof(buffer)-1))>0){
        buffer[n]="\0";
        printf("Telemetria %s", buffer);
    };
    close(fd_pipe);

    pthread_mutex_lock(&trinco);
    veiculosCirculacao--;
    printf("[CONTROLADOR] Veiculos ativos: %d",veiculosCirculacao);
    pthread_mutex_unlock(&trinco);

    return NULL;

};

void mandaVeiculo(int index){
    int fd_anonimo[2];

    if (pipe(fd_anonimo)== -1){
        perror("Erro ao criar pipe anonimo");
    }
    pid_t pid=fork();

    if (pid==0){

        close(fd_anonimo[0]);
        dup2(fd_anonimo[1],STDOUT_FILENO);
        close (fd_anonimo[1]);  


        char arg_id[10];
        char arg_pid_cliente[20];
        char arg_dist[10];


        strintf(arg_id, "%d",viagem[index].id );
        stringf(arg_pid_cliente, "%d", viagem[index].pidCliente);
        sprintf(arg_dist, "%d", viagem[index].distancia);

        execl("./veiculo", "./veiculo", arg_id, arg_pid_cliente, arg_dist, NULL);

        //---------------------------------tava aqui
    }


};

void *processaPedido(void *arg) {

    Pedido p = *(Pedido*)arg;
    free(arg); 

    char fifo_resposta[100];
    Resposta r;

    r.sucesso = 0;
    strcpy(r.mensagem, "Erro desconhecido");

    snprintf(fifo_resposta, sizeof(fifo_resposta), "%s%d", CLIENTE_FIFO_BASE, p.pid_cliente);

    printf("Thread %lu -> Recebi o comando '%s' de '%s' \n", (unsigned long)pthread_self(), p.comando, p.username);
    
    // LOGIN ou NewUser
    if (strcmp(p.comando, "login") == 0) {
        pthread_mutex_lock(&trinco); 

        if (n_users < MAX_USERS) {
            
            int existe = 0;
            for (int i = 0; i < n_users; i++) {
                if(strcmp(utilizadores[i], p.username) == 0) {
                    existe = 1;
                    break;
                }
            }

            if (existe == 0) {
                //Regista
                strncpy(utilizadores[n_users], p.username, TAM_NOME);
                n_users++;
                r.sucesso = 1;
                strcpy(r.mensagem, "Login efetuado com sucesso!");
                printf("Novo utilizador registado: %s\n", p.username);
            } else {
                //Duplicado
                strcpy(r.mensagem, "Erro: Username já está em uso");
                r.sucesso = 0;
            }

        } else {
            //Cheio
            r.sucesso = 0;
            strcpy(r.mensagem, "Maximo de utilizadores atingido");
        }

        pthread_mutex_unlock(&trinco); 
    }

    
    else if (strcmp(p.comando, "agendar") == 0) {

        pthread_mutex_lock(&trinco); 

        if (nViagens < MAX_SERVICOS) {
            int horaV, distV;
            char loc[TAM_ARGUMENTOS];

            if (sscanf(p.args, "%d %s %d",&horaV, loc, &distV )==3){

                viagem[nViagens].id=nViagens +1;
                strncpy (viagem[nViagens].cliente, p.username, TAM_NOME);

                viagem[nViagens].hora=horaV;
                viagem[nViagens].distancia= distV;
                strncpy(viagem[nViagens].origem, loc,TAM_ARGUMENTOS);

                viagem[nViagens].estado=0;

                r.sucesso=1;
                sprintf(r.mensagem, "--Viagem agendada--");
                printf("Nova Viagem ID %d: Cliente=%s, Hora=%d, Origem=%s, Dist=%d\n",viagem[nViagens].id, p.username, horaV, loc, distV);
                nViagens++;
            };
        } else {
            r.sucesso = 0;
            strcpy(r.mensagem, "Lista de serviços cheia");
        }

        pthread_mutex_unlock(&trinco);
    }
    else {
        sprintf(r.mensagem, "Comando desconhecido: %s", p.comando);
    }


    int fd_cli = open(fifo_resposta, O_WRONLY);
    if (fd_cli != -1) {
        write(fd_cli, &r, sizeof(Resposta));
        close(fd_cli);
    }

    return NULL;
}

int main() {
    Pedido p;
    int fd_fifo;

    // FIFO principal
    if (mkfifo(CONTROLADOR_FIFO, 0666) == -1) {
        if (errno != EEXIST) {
            perror("Erro ao criar FIFO Controlador");
            exit(1);
        }
    }

    printf("--------------- Controlador Iniciado -------\n");

    while (1) {
        fd_fifo = open(CONTROLADOR_FIFO, O_RDONLY);
        if (fd_fifo == -1) {
            perror("Erro ao abrir FIFO Controlador");
            continue; 
        }

        // Ler pedido
        int n = read(fd_fifo, &p, sizeof(Pedido));
        close(fd_fifo); 

        if (n == sizeof(Pedido)) {
            pthread_t t;

            Pedido *p_thread = malloc(sizeof(Pedido));//----------------------------------------------
            *p_thread = p;

            if (pthread_create(&t, NULL, processaPedido, p_thread) != 0) {
                perror("Erro ao criar thread");
                free(p_thread);
            } else {
                pthread_detach(t); 
            }
        }
    }

    unlink(CONTROLADOR_FIFO);
    return 0;
}