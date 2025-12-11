#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h> 
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

#include "comum.h" 

#define MAX_USERS 30
#define MAX_SERVICOS 30


typedef struct {
    int id;
    char cliente[TAM_NOME];
    char origem[TAM_ARGUMENTOS]; 
    int distancia; 
    int estado; // 0: Agendado 1: A decorrer 2: Concluido
    int hora;
    int pidCliente;
    int pidVeiculo;
    int km_percorridos_temp;
    int percentagem_atual;
} Viagem;

typedef struct {
    Viagem viagens[MAX_SERVICOS];
    int nViagens;
    char utilizadores[MAX_USERS][TAM_NOME];
    int n_users;
    int maxVeiculos;
    int veiculosCirculacao;
    int tempoDecorrido;
    long long total_km;
    pthread_mutex_t trinco; 
} DadosControl;


typedef struct {
    Pedido p;
    DadosControl *dados; 
} ArgsPedido;

typedef struct {
    int fd_pipe;
    DadosControl *dados; 
} ArgsMonitor;

DadosControl *ptr_dados_global = NULL;



void enviar_resposta(int pid_cliente, Resposta *r) {
    char fifo_resposta[100];
    snprintf(fifo_resposta, sizeof(fifo_resposta), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);
    int fd_cli = open(fifo_resposta, O_WRONLY);
    if (fd_cli != -1) {
        write(fd_cli, r, sizeof(Resposta));
        close(fd_cli);
    }
}

void processa_telemetria(char *linha, DadosControl *d) {
    int id, perc;
    
    pthread_mutex_lock(&d->trinco); 
    
    if (sscanf(linha, "VIAGEM %d %d%%", &id, &perc) == 2) {
        int idx = id - 1;
        if (idx >= 0 && idx < d->nViagens) {
            d->viagens[idx].percentagem_atual = perc;
            
            int km_reais_agora = (d->viagens[idx].distancia * perc) / 100;
            int delta = km_reais_agora - d->viagens[idx].km_percorridos_temp;
            
            if (delta > 0) {
                d->viagens[idx].km_percorridos_temp = km_reais_agora;
                d->total_km += delta;
            }
        }
    }
    else if (strstr(linha, "CONCLUIDA") != NULL || strstr(linha, "CANCELADA") != NULL) {
        int id_concl;
        sscanf(linha, "VIAGEM %d", &id_concl);
        int idx = id_concl - 1;
        
        if (idx >= 0 && idx < d->nViagens && d->viagens[idx].estado == 1) {
            d->viagens[idx].estado = 2; 
            
            if (strstr(linha, "CONCLUIDA")) {
                int falta = d->viagens[idx].distancia - d->viagens[idx].km_percorridos_temp;
                if (falta > 0) d->total_km += falta;
            }

            d->veiculosCirculacao--;
            printf("[CONTROLADOR] Viagem %d terminada. Vagas: %d\n", id_concl, d->maxVeiculos - d->veiculosCirculacao);
        }
    }
    pthread_mutex_unlock(&d->trinco);
}

void *monitorVeiculo(void *arg){
    ArgsMonitor *args = (ArgsMonitor*)arg;
    int fd_pipe = args->fd_pipe;
    DadosControl *d = args->dados;
    free(args); 

    char buffer[1024];
    ssize_t n;
    char resto[1024] = "";

    while((n = read(fd_pipe, buffer, sizeof(buffer)-1)) > 0){
        buffer[n] = '\0';
        char temp[2048];
        snprintf(temp, sizeof(temp), "%s%s", resto, buffer);
        
        char *inicio = temp;
        char *fim_linha;
        
        while ((fim_linha = strchr(inicio, '\n')) != NULL) {
            *fim_linha = '\0';
            processa_telemetria(inicio, d); 
            inicio = fim_linha + 1;
        }
        strcpy(resto, inicio);
    };
    close(fd_pipe);
    return NULL;
};

void mandaVeiculo(int index, DadosControl *d){
    int fd_anonimo[2];

    if (pipe(fd_anonimo) == -1){
        perror("Erro ao criar pipe anonimo");
        return;
    }
    pid_t pid = fork();

    if (pid == -1){
        perror("Erro no fork");
        return;
    }

    if (pid == 0){
        close(fd_anonimo[0]);
        if (dup2(fd_anonimo[1], STDOUT_FILENO) == -1){
            perror("Erro no dup2");
            exit(1);
        }
        close(fd_anonimo[1]);  

        char arg_id[10], arg_pid[20], arg_dist[10];
        sprintf(arg_id, "%d", d->viagens[index].id);
        sprintf(arg_pid, "%d", d->viagens[index].pidCliente);
        sprintf(arg_dist, "%d", d->viagens[index].distancia);

        execl("./veiculo", "./veiculo", arg_id, arg_pid, arg_dist, NULL);
        perror("Erro ao executar veiculo");
        exit(1);

    } else {
        close(fd_anonimo[1]);
        printf("Veiculo lançado para a viagem %d\n", d->viagens[index].id);

        d->veiculosCirculacao++;
        d->viagens[index].estado = 1;
        d->viagens[index].pidVeiculo = pid;

        pthread_t t_monitor;
        ArgsMonitor *args_mon = malloc(sizeof(ArgsMonitor));
        args_mon->fd_pipe = fd_anonimo[0];
        args_mon->dados = d; 

        if (pthread_create(&t_monitor, NULL, monitorVeiculo, args_mon) != 0){
            perror("Erro ao criar thread monitor");
            free(args_mon);
        } else {
            pthread_detach(t_monitor);
        }
    }
}

void *processaPedido(void *arg) {
    ArgsPedido *args = (ArgsPedido*)arg;
    Pedido p = args->p;
    DadosControl *d = args->dados;
    free(args); 

    Resposta r;
    r.sucesso = 0;
    strcpy(r.mensagem, "Erro desconhecido");
    strcpy(r.dados_extra, "");

    printf("[Debug] Thread %lu a processar pedido de %s\n", (unsigned long)pthread_self(), p.username);
    
    if (strcmp(p.comando, "login") == 0) {
        pthread_mutex_lock(&d->trinco); 
        if (d->n_users < MAX_USERS) {
            int existe = 0;
            for (int i = 0; i < d->n_users; i++) {
                if(strcmp(d->utilizadores[i], p.username) == 0) {
                    existe = 1; break;
                }
            }
            if (!existe) {
                strncpy(d->utilizadores[d->n_users], p.username, TAM_NOME);
                d->n_users++;
                r.sucesso = 1;
                strcpy(r.mensagem, "Login com sucesso\n");
                printf("Novo utilizador: %s\nADMIN>", p.username);
            } else {
                strcpy(r.mensagem, "User ja existe\n");
            }
        } else {
            strcpy(r.mensagem, "Limite de users atingido\n");
        }
        pthread_mutex_unlock(&d->trinco); 
        enviar_resposta(p.pid_cliente, &r);
    }
    else if (strcmp(p.comando, "agendar") == 0) {
        pthread_mutex_lock(&d->trinco); 
        if (d->nViagens < MAX_SERVICOS) {
            int horaV, distV;
            char loc[TAM_ARGUMENTOS];
            if (sscanf(p.args, "%d %s %d", &horaV, loc, &distV) == 3){
                if (horaV <= d->tempoDecorrido) {
                    strcpy(r.mensagem, "Erro: Hora invalida (ja passou)");
                } else {
                    int i = d->nViagens;
                    d->viagens[i].id = i + 1;
                    strncpy(d->viagens[i].cliente, p.username, TAM_NOME);
                    d->viagens[i].hora = horaV;
                    d->viagens[i].distancia = distV;
                    strncpy(d->viagens[i].origem, loc, TAM_ARGUMENTOS);
                    d->viagens[i].pidCliente = p.pid_cliente; 
                    d->viagens[i].estado = 0;
                    
                    r.sucesso = 1;
                    sprintf(r.mensagem, "Viagem agendada (ID %d)", d->viagens[i].id);
                    printf("\nNova Viagem %d: %s -> %dKm as %dh\n", d->viagens[i].id, loc, distV, horaV);
                    d->nViagens++;
                }
            }
        } else {
            strcpy(r.mensagem, "Lista de servicos cheia");
        }
        pthread_mutex_unlock(&d->trinco);
        enviar_resposta(p.pid_cliente, &r);
    }
    else if (strcmp(p.comando, "consultar") == 0) {
        pthread_mutex_lock(&d->trinco);
        int enc = 0;
        for(int i=0; i<d->nViagens; i++){
            if (d->viagens[i].pidCliente == p.pid_cliente) {
                snprintf(r.mensagem, TAM_MENSAGEM, "ID:%d Hora:%d Dest:%.15s Est:%d", 
                         d->viagens[i].id, d->viagens[i].hora, d->viagens[i].origem, d->viagens[i].estado);
                enviar_resposta(p.pid_cliente, &r);
                enc = 1;
            }
        }
        if(!enc) {
            strcpy(r.mensagem, "Sem viagens agendadas.");
            enviar_resposta(p.pid_cliente, &r);
        }
        pthread_mutex_unlock(&d->trinco);
    }
    else if (strcmp(p.comando, "cancelar") == 0) {
        int id_c = atoi(p.args);
        int count = 0;
        pthread_mutex_lock(&d->trinco);
        
        for(int i=0; i<d->nViagens; i++){
            // Cancela se for o ID certo ou se ID for 0 (todos)
            if (d->viagens[i].pidCliente == p.pid_cliente && d->viagens[i].estado != 2) {
                if (id_c == 0 || d->viagens[i].id == id_c) {
                    if (d->viagens[i].estado == 1) {
                        kill(d->viagens[i].pidVeiculo, SIGUSR1);
                    } else {
                        d->viagens[i].estado = 2; // Cancelada
                    }
                    count++;
                }
            }
        }
        pthread_mutex_unlock(&d->trinco);
        sprintf(r.mensagem, "Cancelados: %d", count);
        enviar_resposta(p.pid_cliente, &r);
    }
    else {
        sprintf(r.mensagem, "Comando desconhecido: %s", p.comando);
        enviar_resposta(p.pid_cliente, &r);
    }

    return NULL;
}

// THREAD 4 (Timer): Controla o tempo e lança veículos
void *threadTempo(void *arg){
    DadosControl *d = (DadosControl*)arg; // Recebe ponteiro para a struct
    printf("Thread Timer ativada\n");

    while (1){
        sleep(1);
        
        pthread_mutex_lock(&d->trinco); 

        d->tempoDecorrido++;

        for (int i=0; i < d->nViagens; i++){ 
            if(d->viagens[i].estado == 0 && d->viagens[i].hora <= d->tempoDecorrido){
                if(d->veiculosCirculacao < d->maxVeiculos) {
                    printf("\n[TIMER] Viagem %d iniciada (Cli: %s) as %d\n", 
                           d->viagens[i].id, d->viagens[i].cliente, d->tempoDecorrido);
                    mandaVeiculo(i, d); 
                }
            }
        }
        pthread_mutex_unlock(&d->trinco); 
    }
    return NULL;    
}

void *threadAdmin(void *arg){
    DadosControl *d = (DadosControl*)arg; 
    char cmd[100];
    printf("Thread Admin ativa\n");

    while(1){
        printf("ADMIN>");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin)==NULL) break;
        cmd[strcspn(cmd, "\n")]=0;
        char *comando = strtok(cmd, " ");
        if (!comando) continue;

        pthread_mutex_lock(&d->trinco); 

        if (strcmp(comando, "listar")==0){
            printf("Viagens (%d):\n", d->nViagens);
            for(int i=0; i < d->nViagens; i++){
                char *st = (d->viagens[i].estado==0) ? "Agendado" : 
                           (d->viagens[i].estado==1) ? "A decorrer" : "Concluido";
                printf("ID:%d | Cli:%s | Hora:%d | Est:%s\n", 
                       d->viagens[i].id, d->viagens[i].cliente, d->viagens[i].hora, st);
            }
        } 
        else if(strcmp(comando, "hora")==0){
            printf("Tempo: %d\n", d->tempoDecorrido);
        }
        else if (strcmp(comando, "km")==0){
            printf("Total Km: %lld\n", d->total_km);
        }
        else if (strcmp(comando, "frota") == 0) {
            printf("Frota: %d/%d\n", d->veiculosCirculacao, d->maxVeiculos);
            for(int i=0; i<d->nViagens; i++) {
                if (d->viagens[i].estado == 1)
                    printf("  > V. na Viagem %d: %d%%\n", d->viagens[i].id, d->viagens[i].percentagem_atual);
            }
        }
        else if (strcmp(comando, "utiliz") == 0) {
            printf("Users (%d):\n", d->n_users);
            for(int i=0; i<d->n_users; i++) printf(" - %s\n", d->utilizadores[i]);
        }
        else if (strcmp(comando, "cancelar")==0) {
            char *arg = strtok(NULL, " ");
            if (arg) {
                int id = atoi(arg);
                int idx = id - 1;
                if (idx >= 0 && idx < d->nViagens && d->viagens[idx].estado != 2) {
                    if (d->viagens[idx].estado == 1) kill(d->viagens[idx].pidVeiculo, SIGUSR1);
                    else d->viagens[idx].estado = 2;
                    printf("Viagem %d cancelada.\n", id);
                }
            }
        }
        else if(strcmp(comando, "terminar")==0){
            printf("Encerrando...\n");
            for(int i=0; i<d->nViagens; i++) 
                if(d->viagens[i].estado == 1) kill(d->viagens[i].pidVeiculo, SIGUSR1);
            
            pthread_mutex_unlock(&d->trinco); 
            exit(0);
        }
        pthread_mutex_unlock(&d->trinco);
    }
    return NULL;    
}

void trata_ctrl_c(int s) {
    (void)s;
    printf("\nControlador -> a limpar para fechar programa\n");

    if (ptr_dados_global) {
        for(int i = 0; i < ptr_dados_global->nViagens; i++) {
            if (ptr_dados_global->viagens[i].estado == 1) {
                kill(ptr_dados_global->viagens[i].pidVeiculo, SIGUSR1);
            }
        }
    }

    unlink(CONTROLADOR_FIFO);
    exit(0);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, trata_ctrl_c);
    setbuf(stdout, NULL);

    DadosControl dados; 
    dados.nViagens = 0;
    dados.n_users = 0;
    dados.veiculosCirculacao = 0;
    dados.total_km = 0;
    dados.tempoDecorrido = 0;
    
    pthread_mutex_init(&dados.trinco, NULL);

    ptr_dados_global = &dados;

    char *env_nveiculos = getenv("NVEICULOS");
    dados.maxVeiculos = (env_nveiculos) ? atoi(env_nveiculos) : 10;
    printf("Max Veiculos: %d\n", dados.maxVeiculos);
    
    if (mkfifo(CONTROLADOR_FIFO, 0666) == -1 && errno != EEXIST) {
        perror("Erro FIFO"); exit(1);
    }
    printf("--------------- Controlador Iniciado -------\n");

    
    pthread_t t_timer, t_admin;
    
    if(pthread_create(&t_timer, NULL, threadTempo, (void*)&dados) != 0) {
         perror("Erro thread tempo"); exit(1);
    }
    pthread_detach(t_timer);

    if(pthread_create(&t_admin, NULL, threadAdmin, (void*)&dados) != 0) {
         perror("Erro thread admin"); exit(1);
    }
    pthread_detach(t_admin);

    
    int fd_fifo;
    Pedido p;
    while (1) {
        fd_fifo = open(CONTROLADOR_FIFO, O_RDONLY);
        if (fd_fifo == -1) continue; 

        while(read(fd_fifo, &p, sizeof(Pedido)) > 0) {
            pthread_t t;
            
            ArgsPedido *args = malloc(sizeof(ArgsPedido));
            args->p = p;
            args->dados = &dados; 

            if (pthread_create(&t, NULL, processaPedido, (void*)args) != 0) {
                free(args);
                perror("Erro criar thread pedido");
            } else {
                pthread_detach(t); 
            }
        }
        close(fd_fifo); 
    }

    unlink(CONTROLADOR_FIFO);
    pthread_mutex_destroy(&dados.trinco);
    return 0;
}