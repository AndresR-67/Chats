#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#define SHM_KEY 1234
#define SEM_KEY 5678
#define MAX_MSG 256
#define STOP_WORD "adios\n"

// Estructura de la memoria compartida
typedef struct {
    char buffer1[MAX_MSG]; // Mensajes de usuario 1 a 2
    char buffer2[MAX_MSG]; // Mensajes de usuario 2 a 1
} ChatMemory;

// Semáforo: operaciones
void sem_wait(int semid, int semnum) {
    struct sembuf op = { semnum, -1, 0 };
    semop(semid, &op, 1);
}

void sem_signal(int semid, int semnum) {
    struct sembuf op = { semnum, +1, 0 };
    semop(semid, &op, 1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [1|2]\n", argv[0]);
        exit(1);
    }

    int user = atoi(argv[1]);

    // Crear o acceder a la memoria compartida
    int shmid = shmget(SHM_KEY, sizeof(ChatMemory), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }

    ChatMemory *chat = (ChatMemory *)shmat(shmid, NULL, 0);
    if (chat == (void *)-1) {
        perror("shmat");
        exit(1);
    }

    // Crear o acceder al semáforo
    int semid = semget(SEM_KEY, 2, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget");
        exit(1);
    }

    // Inicializar semáforos si es el primer usuario
    if (user == 1) {
        semctl(semid, 0, SETVAL, 1); // Turno de usuario 1
        semctl(semid, 1, SETVAL, 0); // Turno de usuario 2
    }

    printf("Chat iniciado. Escribe \"%s\" para salir.\n", STOP_WORD);

    char msg[MAX_MSG];
    while (1) {
        if (user == 1) {
            sem_wait(semid, 0); // Espera turno usuario 1
            printf("Tú: ");
            fgets(msg, MAX_MSG, stdin);
            strcpy(chat->buffer1, msg);

            if (strcmp(msg, STOP_WORD) == 0) break;

            sem_signal(semid, 1); // Da turno a usuario 2
            sem_wait(semid, 0);   // Espera su turno para leer

            if (strcmp(chat->buffer2, STOP_WORD) == 0) break;
            printf("Otro: %s", chat->buffer2);

            sem_signal(semid, 0); // Devuelve el turno
        }
        else if (user == 2) {
            sem_wait(semid, 1); // Espera turno usuario 2
            printf("Tú: ");
            fgets(msg, MAX_MSG, stdin);
            strcpy(chat->buffer2, msg);

            if (strcmp(msg, STOP_WORD) == 0) break;

            sem_signal(semid, 0); // Da turno a usuario 1
            sem_wait(semid, 1);   // Espera su turno para leer

            if (strcmp(chat->buffer1, STOP_WORD) == 0) break;
            printf("Otro: %s", chat->buffer1);

            sem_signal(semid, 1); // Devuelve el turno
        }
    }

    // Limpieza si eres el proceso 1 (asumimos que saldrá último)
    if (user == 1) {
        shmdt(chat);
        shmctl(shmid, IPC_RMID, NULL);
        semctl(semid, 0, IPC_RMID);
    } else {
        shmdt(chat);
    }

    printf("Saliendo del chat...\n");
    return 0;
}
