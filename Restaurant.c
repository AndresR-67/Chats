#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define SHM_KEY 1234
#define SEM_KEY 5678
#define MAX_PEDIDOS 10
#define MAX_CLIENTES 3
#define PEDIDO_LEN 64

typedef struct {
    int cliente_id;
    char pedido[PEDIDO_LEN];
    int en_uso;
    int recibido;
    int preparado;
} Pedido;

typedef struct {
    Pedido pedidos[MAX_PEDIDOS];
    int inicio;
    int fin;
    int cantidad;
} ColaPedidos;

union semun {
    int val;
};

void sem_wait(int semid, int semnum) {
    struct sembuf op = { semnum, -1, 0 };
    semop(semid, &op, 1);
}

void sem_signal(int semid, int semnum) {
    struct sembuf op = { semnum, +1, 0 };
    semop(semid, &op, 1);
}

void cliente(int id) {
    int shmid = shmget(SHM_KEY, sizeof(ColaPedidos), IPC_CREAT | 0666);
    ColaPedidos* cola = (ColaPedidos*)shmat(shmid, NULL, 0);
    int semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);

    while (1) {
        char input[PEDIDO_LEN];
        printf("Cliente %d - Ingrese pedido (o 'salir'): ", id);
        fgets(input, PEDIDO_LEN, stdin);
        input[strcspn(input, "\n")] = 0;

        if (strcmp(input, "salir") == 0) break;

        sem_wait(semid, 0);

        if (cola->cantidad >= MAX_PEDIDOS) {
            printf("Cola llena. Intente más tarde.\n");
            sem_signal(semid, 0);
            continue;
        }

        Pedido* p = &cola->pedidos[cola->fin];
        p->cliente_id = id;
        strncpy(p->pedido, input, PEDIDO_LEN);
        p->en_uso = 1;
        p->recibido = 0;
        p->preparado = 0;

        cola->fin = (cola->fin + 1) % MAX_PEDIDOS;
        cola->cantidad++;

        sem_signal(semid, 0);

        printf("Cliente %d - Esperando confirmación de cocina...\n", id);
        while (!p->recibido) usleep(50000);
        printf("Cliente %d - Pedido recibido\n", id);
        while (!p->preparado) usleep(50000);
        printf("Cliente %d - Pedido preparado: %s\n", id, p->pedido);
    }

    shmdt(cola);
}

void cocina() {
    int shmid = shmget(SHM_KEY, sizeof(ColaPedidos), IPC_CREAT | 0666);
    ColaPedidos* cola = (ColaPedidos*)shmat(shmid, NULL, 0);
    int semid = semget(SEM_KEY, 1, IPC_CREAT | 0666);

    union semun arg;
    arg.val = 1;
    semctl(semid, 0, SETVAL, arg);

    printf("Cocina iniciada. Escriba 'close' para salir.\n");

    while (1) {
        sem_wait(semid, 0);

        if (cola->cantidad == 0) {
            sem_signal(semid, 0);
            usleep(100000);
            continue;
        }

        Pedido* p = &cola->pedidos[cola->inicio];

        printf("Cocina - Procesando pedido de Cliente %d: %s\n", p->cliente_id, p->pedido);
        p->recibido = 1;
        sleep(2); // Simula preparación
        p->preparado = 1;

        cola->inicio = (cola->inicio + 1) % MAX_PEDIDOS;
        cola->cantidad--;

        sem_signal(semid, 0);
    }

    shmdt(cola);
}

#include <string.h>

int main() {
    extern int argc;
    extern char **argv;

    if (argc != 2) {
        printf("Uso: %s [cocina | cliente1 | cliente2 | cliente3]\n", argv[0]);
        return 1;
    }

    char *modo = argv[1];

    if (strcmp(modo, "cocina") == 0) {
        cocina();
    } else if (strcmp(modo, "cliente1") == 0) {
        cliente(1);
    } else if (strcmp(modo, "cliente2") == 0) {
        cliente(2);
    } else if (strcmp(modo, "cliente3") == 0) {
        cliente(3);
    } else {
        printf("Modo inválido: %s\n", modo);
        printf("Usa 'cocina' o 'cliente1' a 'cliente3'\n");
        return 1;
    }

    return 0;
}
