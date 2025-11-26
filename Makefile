CC = gcc
CFLAGS = -Wall -Wextra -pthread -g

all: controlador cliente veiculo

controlador: controlador.c
	$(CC) $(CFLAGS) -o controlador controlador.c

cliente: cliente.c
	$(CC) $(CFLAGS) -o cliente cliente.c

veiculo: veiculo.c
	$(CC) $(CFLAGS) -o veiculo veiculo.c

clean:
	rm -f controlador cliente veiculo *.o