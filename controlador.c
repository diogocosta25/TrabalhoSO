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
    int estado;
    int hora;
    int pidCliente;
    int pidVeiculo;
    int km_percorridos_temp;
    int percentagem_atual;
} Viagem;

Viagem viagem[MAX_SERVICOS];
int nViagens=0;

char utilizadores[MAX_USERS][TAM_NOME];
int n_users = 0;

int maxVeiculos=0;
int veiculosCirculacao=0;
int tempoDecorrido=0;
long long total_km = 0;

pthread_mutex_t trinco = PTHREAD_MUTEX_INITIALIZER;

void enviar_resposta(int pid_cliente, Resposta *r) {
    char fifo_resposta[100];
    snprintf(fifo_resposta, sizeof(fifo_resposta), "%s%d", CLIENTE_FIFO_BASE, pid_cliente);
    int fd_cli = open(fifo_resposta, O_WRONLY);
    if (fd_cli != -1) {
        write(fd_cli, r, sizeof(Resposta));
        close(fd_cli);
    }
}

void processa_telemetria(char *linha) {
    int id, perc;
    
    pthread_mutex_lock(&trinco);
    
    if (sscanf(linha, "VIAGEM %d %d%%", &id, &perc) == 2) {
        int idx = id - 1;
        if (idx >= 0 && idx < nViagens) {
            viagem[idx].percentagem_atual = perc;
            
            int km_reais_agora = (viagem[idx].distancia * perc) / 100;
            int delta = km_reais_agora - viagem[idx].km_percorridos_temp;
            
            if (delta > 0) {
                viagem[idx].km_percorridos_temp = km_reais_agora;
                total_km += delta;
            }
        }
    }
    else if (strstr(linha, "CONCLUIDA") != NULL || strstr(linha, "CANCELADA") != NULL) {
        int id_concl;
        sscanf(linha, "VIAGEM %d", &id_concl);
        int idx = id_concl - 1;
        
        if (idx >= 0 && idx < nViagens && viagem[idx].estado == 1) {
            viagem[idx].estado = 2;
            
            if (strstr(linha, "CONCLUIDA")) {
                int falta = viagem[idx].distancia - viagem[idx].km_percorridos_temp;
                if (falta > 0) total_km += falta;
            }

            veiculosCirculacao--;
            printf("[CONTROLADOR] Viagem %d terminada. Vagas: %d\n", id_concl, maxVeiculos - veiculosCirculacao);
        }
    }
    pthread_mutex_unlock(&trinco);
}

void *monitorVeiculo(void *arg){
    int fd_pipe= *(int*)arg;
    free(arg);

    char buffer[1024];
    ssize_t n;
    char resto[1024] = "";

    while((n=read(fd_pipe, buffer, sizeof(buffer)-1))>0){
        buffer[n]='\0';
        
        char temp[2048];
        snprintf(temp, sizeof(temp), "%s%s", resto, buffer);
        
        char *inicio = temp;
        char *fim_linha;
        
        while ((fim_linha = strchr(inicio, '\n')) != NULL) {
            *fim_linha = '\0';
            processa_telemetria(inicio);
            inicio = fim_linha + 1;
        }
        strcpy(resto, inicio);
    };
    close(fd_pipe);
    return NULL;
};

void mandaVeiculo(int index){
    int fd_anonimo[2];

    if (pipe(fd_anonimo)== -1){
        perror("Erro ao criar pipe anonimo");
        return;
    }
    pid_t pid=fork();

    if (pid==-1){
        perror("Erro no fork");
        return;
    }

    if (pid == 0){
        close(fd_anonimo[0]);

        if (dup2(fd_anonimo[1],STDOUT_FILENO) == -1){
            perror("Erro no dup2");
            exit(1);
        }
        close (fd_anonimo[1]);  

        char arg_id[10];
        char arg_pid_cliente[20];
        char arg_dist[10];

        sprintf(arg_id, "%d",viagem[index].id );
        sprintf(arg_pid_cliente, "%d", viagem[index].pidCliente);
        sprintf(arg_dist, "%d", viagem[index].distancia);

        execl("./veiculo", "./veiculo", arg_id, arg_pid_cliente, arg_dist, NULL);

        perror("Erro ao executar veiculo");
        exit(1);

    }else {
        close(fd_anonimo[1]);

        printf("Veiculo lançado para a viagem %d\n",viagem[index].id);

        veiculosCirculacao++;
        viagem[index].estado = 1;
        viagem[index].pidVeiculo = pid;

        pthread_t t_monitor;
        int *fd_leitura = malloc(sizeof(int));
        *fd_leitura = fd_anonimo[0];

        if (pthread_create(&t_monitor, NULL, monitorVeiculo, fd_leitura)!= 0){
            perror("Erro ao criar thread de monotorização");
        }else {
            pthread_detach(t_monitor);
        }
    }
}

void *processaPedido(void *arg) {
    Pedido p = *(Pedido*)arg;
    free(arg); 

    Resposta r;
    r.sucesso = 0;
    strcpy(r.mensagem, "Erro desconhecido");
    strcpy(r.dados_extra, "");

    printf("\nThread %lu -> Recebi o comando '%s' de '%s' \n", (unsigned long)pthread_self(), p.comando, p.username);
    
    if (strcmp(p.comando, "login") == 0) {
        pthread_mutex_lock(&trinco); 
        if (n_users < MAX_USERS) {
            int existe = 0;
            for (int i = 0; i < n_users; i++) {
                if(strcmp(utilizadores[i], p.username) == 0) {
                    existe = 1; break;
                }
            }
            if (existe == 0) {
                strncpy(utilizadores[n_users], p.username, TAM_NOME);
                n_users++;
                r.sucesso = 1;
                strcpy(r.mensagem, "Login efetuado com sucesso\n");
                printf("Novo utilizador registado: %s\n", p.username);
                printf("ADMIN>");
            } else {
                strcpy(r.mensagem, "Erro: Username já está em uso\n");
                r.sucesso = 0;
            }
        } else {
            r.sucesso = 0;  
            strcpy(r.mensagem, "Maximo de utilizadores atingido\n");
        }
        pthread_mutex_unlock(&trinco); 
        enviar_resposta(p.pid_cliente, &r);
    }
    else if (strcmp(p.comando, "agendar") == 0) {
        pthread_mutex_lock(&trinco); 
        if (nViagens < MAX_SERVICOS) {
            int horaV, distV;
            char loc[TAM_ARGUMENTOS];
            if (sscanf(p.args, "%d %s %d",&horaV, loc, &distV )==3){
                if (horaV <= tempoDecorrido) {
                    r.sucesso = 0;
                    strcpy(r.mensagem, "Erro: Hora invalida (ja passou)");
                } else {
                    viagem[nViagens].id=nViagens +1;
                    strncpy (viagem[nViagens].cliente, p.username, TAM_NOME);
                    viagem[nViagens].hora=horaV;
                    viagem[nViagens].distancia= distV;
                    strncpy(viagem[nViagens].origem, loc,TAM_ARGUMENTOS);
                    viagem[nViagens].pidCliente = p.pid_cliente; 
                    viagem[nViagens].estado=0;
                    r.sucesso=1;
                    sprintf(r.mensagem, "--Viagem agendada--");
                    printf("\nNova Viagem ID %d: Cliente=%s, Hora=%d, Origem=%s, Dist=%d\n",viagem[nViagens].id, p.username, horaV, loc, distV);
                    nViagens++;
                }
            }
        } else {
            r.sucesso = 0;
            strcpy(r.mensagem, "Lista de serviços cheia\n");
        }
        pthread_mutex_unlock(&trinco);
        enviar_resposta(p.pid_cliente, &r);
    }
    else if (strcmp(p.comando, "consultar") == 0) {
        pthread_mutex_lock(&trinco);
        int encontrou = 0;
        for(int i=0; i<nViagens; i++){
            if (viagem[i].pidCliente == p.pid_cliente) {
                snprintf(r.mensagem, TAM_MENSAGEM, "ID:%d Hora:%d Dest:%.20s Est:%d", 
                         viagem[i].id, viagem[i].hora, viagem[i].origem, viagem[i].estado);
                enviar_resposta(p.pid_cliente, &r);
                encontrou = 1;
            }
        }
        if(!encontrou) {
            strcpy(r.mensagem, "Sem viagens agendadas.");
            enviar_resposta(p.pid_cliente, &r);
        }
        pthread_mutex_unlock(&trinco);
    }
    else if (strcmp(p.comando, "cancelar") == 0) {
        int id_cancelar = atoi(p.args);
        int cancelado = 0;
        pthread_mutex_lock(&trinco);
        if (id_cancelar == 0) { 
             for(int i=0; i<nViagens; i++){
                 if (viagem[i].pidCliente == p.pid_cliente && viagem[i].estado != 2) {
                     if (viagem[i].estado == 1) {
                         kill(viagem[i].pidVeiculo, SIGUSR1);
                     } else {
                         viagem[i].estado = 2;
                     }
                     cancelado++;
                 }
             }
        } else {
            int idx = id_cancelar - 1;
            if (idx >= 0 && idx < nViagens && viagem[idx].pidCliente == p.pid_cliente && viagem[idx].estado != 2) {
                if (viagem[idx].estado == 1) {
                    kill(viagem[idx].pidVeiculo, SIGUSR1);
                } else {
                    viagem[idx].estado = 2;
                }
                cancelado = 1;
            }
        }
        pthread_mutex_unlock(&trinco);
        sprintf(r.mensagem, "Cancelados: %d servicos", cancelado);
        enviar_resposta(p.pid_cliente, &r);
    }
    else {
        sprintf(r.mensagem, "Comando desconhecido: %s \n", p.comando);
        enviar_resposta(p.pid_cliente, &r);
    }

    return NULL;
}

void *threadTempo(void *arg){
    (void)arg;
    printf("Thread para timer ativada \n");

    while (1){
        sleep(1);
        pthread_mutex_lock(&trinco);

        tempoDecorrido++;

        for (int i=0; i<nViagens; i++){ 
            if(viagem[i].estado==0 && viagem[i].hora <= tempoDecorrido){
                if(veiculosCirculacao < maxVeiculos) {
                    printf("\nTimer: Viagem id: %d , inciada (Cliente: %s) ás %d horas \n", viagem[i].id, viagem[i].cliente, tempoDecorrido);
                    mandaVeiculo(i);
                }
            }
        }
        pthread_mutex_unlock(&trinco);
    }
    return NULL;    
}

void *threadAdmin(void *args){
    (void)args;
    char cmd[100];
    printf("Comandos admin ativos \n");

    while(1){
        printf("ADMIN>");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin)==NULL){
            break;
        }

        cmd[strcspn(cmd, "\n")]=0;

        char *comando=strtok(cmd, " ");
        if (comando==NULL){
            continue;   
        }

        pthread_mutex_lock(&trinco);

        if (strcmp(comando, "listar")==0){
            printf("->Lista de servicos agendados (%d totais)\n", nViagens);
            for(int i=0;i<nViagens;i++){
                char *estado_str;
                switch(viagem[i].estado){
                    case 0: estado_str="Agendado";break;
                    case 1: estado_str="A decorrer";break;
                    case 2: estado_str="Concluido";break;
                    default: estado_str= "Desconhecido";
                }
                printf("ID: %d | Cliente: %s | Origem: %s | Distância: %d Km | Hora: %d | Estado: %s\n",  viagem[i].id, viagem[i].cliente, viagem[i].origem, viagem[i].distancia, viagem[i].hora, estado_str);
            }
            printf("======================\n");
            
        }else if(strcmp(comando, "hora")==0){
            printf("Tempo Atual: %d \n", tempoDecorrido);
        }else if (strcmp(comando, "km")==0){
            printf("Total de Quilómetros Percorridos: %lld Km\n", total_km);
        }
        else if (strcmp(comando, "frota") == 0) {
            printf("-> Estado da Frota (Ativos: %d / Max: %d)\n", veiculosCirculacao, maxVeiculos);
            int ativos = 0;
            for(int i = 0; i < nViagens; i++) {
                if (viagem[i].estado == 1) {
                    printf("  > Veículo na Viagem %d: %d%% completo (Cliente: %s)\n", viagem[i].id, viagem[i].percentagem_atual, viagem[i].cliente);
                    ativos++;
                }
            }
            if (ativos == 0) printf("  (Nenhum veículo em circulação)\n");
            printf("======================\n");
        }
        else if (strcmp(comando, "utiliz") == 0) {
            printf("-> Utilizadores Logados (%d/%d)\n", n_users, MAX_USERS);
            for(int i = 0; i < n_users; i++) {
                printf("  - %s\n", utilizadores[i]);
            }
            printf("======================\n");
        }
        else if (strcmp(comando, "cancelar")==0) {
            char *arg = strtok(NULL, " ");
            if (arg) {
                int id = atoi(arg);
                int idx = id - 1;
                if (idx >= 0 && idx < nViagens && viagem[idx].estado != 2) {
                    if (viagem[idx].estado == 1) {
                         kill(viagem[idx].pidVeiculo, SIGUSR1);
                    } else {
                         viagem[idx].estado = 2;
                    }
                    printf("Viagem %d cancelada pelo admin.\n", id);
                } else {
                    printf("Viagem não encontrada ou já concluída.\n");
                }
            } else {
                printf("Uso: cancelar <id>\n");
            }
        }
        else if(strcmp(comando, "terminar")==0){
            printf("ADMIN->A encerrar o sistema\n");
            for(int i=0; i<nViagens; i++) 
                if(viagem[i].estado == 1) kill(viagem[i].pidVeiculo, SIGUSR1);
            
            pthread_mutex_unlock(&trinco);
            exit(0);
        }else{
            printf("Comando desconhecido\n");
        }
        pthread_mutex_unlock(&trinco);
    }
    return NULL;    
}

void trata_ctrl_c(int s) {
    (void)s;
    printf("\n[CONTROLADOR] A encerrar por CTRL+C...\n");

    pthread_mutex_lock(&trinco);
    for(int i = 0; i < nViagens; i++) {
        if (viagem[i].estado == 1) {
            printf("A parar veiculo da viagem %d (PID: %d)...\n", viagem[i].id, viagem[i].pidVeiculo);
            kill(viagem[i].pidVeiculo, SIGUSR1);
        }
    }
    pthread_mutex_unlock(&trinco);

    unlink(CONTROLADOR_FIFO);
    
    exit(0);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGINT, trata_ctrl_c);
    setbuf(stdout, NULL);

    char *env_nveiculos = getenv("NVEICULOS");
    if(env_nveiculos){
        maxVeiculos = atoi(env_nveiculos);
    } else {
        maxVeiculos = 10;
    }
    printf("Max Veiculos: %d\n", maxVeiculos);
    
    Pedido p;
    int fd_fifo;

    if (mkfifo(CONTROLADOR_FIFO, 0666) == -1) {
        if (errno != EEXIST) {
            perror("Erro ao criar FIFO Controlador");
            exit(1);
        }
    }

    printf("--------------- Controlador Iniciado -------\n");
    
    pthread_t t_timer;
    if (pthread_create(&t_timer, NULL, threadTempo, NULL)!=0){
        perror("ERRO ao criar thread para o timer");
        unlink(CONTROLADOR_FIFO);
        exit(1);
    }
    pthread_detach(t_timer);

    pthread_t t_admin;
    if(pthread_create(&t_admin,NULL,threadAdmin,NULL)!=0){
        perror("ERRO ao criar thread para o administrador");
        unlink(CONTROLADOR_FIFO);
        exit(1);
    }
    pthread_detach(t_admin);

    while (1) {
        fd_fifo = open(CONTROLADOR_FIFO, O_RDONLY);
        if (fd_fifo == -1) {
            perror("Erro ao abrir FIFO Controlador");
            continue; 
        }

        while(read(fd_fifo, &p, sizeof(Pedido)) > 0) {
            pthread_t t;
            Pedido *p_thread = malloc(sizeof(Pedido));
            *p_thread = p;

            if (pthread_create(&t, NULL, processaPedido, p_thread) != 0) {
                perror("Erro ao criar thread");
                free(p_thread);
            } else {
                pthread_detach(t); 
            }
        }
        close(fd_fifo); 
    }

    unlink(CONTROLADOR_FIFO);
    return 0;
}