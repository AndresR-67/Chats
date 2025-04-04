#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FIFO1 "fifo1"
#define FIFO2 "fifo2"
#define MAX_MSG 256
#define STOP_WORD "adios\n"

void chat(const char *read_fifo, const char *write_fifo) {
    char msg[MAX_MSG];
    int fd_read, fd_write;

    // Abrimos el FIFO de escritura primero (importante para evitar bloqueo)
    fd_write = open(write_fifo, O_WRONLY);
    if (fd_write == -1) {
        perror("Error al abrir FIFO de escritura");
        exit(1);
    }

    fd_read = open(read_fifo, O_RDONLY);
    if (fd_read == -1) {
        perror("Error al abrir FIFO de lectura");
        close(fd_write);
        exit(1);
    }

    printf("Chat iniciado. Escribe \"%s\" para salir.\n", STOP_WORD);

    pid_t pid = fork();

    if (pid == 0) {
        // Proceso hijo: lectura
        while (1) {
            memset(msg, 0, MAX_MSG);
            if (read(fd_read, msg, MAX_MSG) > 0) {
                if (strcmp(msg, STOP_WORD) == 0) {
                    printf("\nEl otro usuario ha salido del chat.\n");
                    exit(0);
                }
                printf("Otro: %s", msg);
                fflush(stdout);
            }
        }
    } else {
        // Proceso padre: escritura
        while (1) {
            memset(msg, 0, MAX_MSG);
            fgets(msg, MAX_MSG, stdin);
            write(fd_write, msg, strlen(msg));
            if (strcmp(msg, STOP_WORD) == 0) {
                printf("Saliendo del chat...\n");
                kill(pid, SIGTERM);
                exit(0);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [1|2]\n", argv[0]);
        printf("1 = Usuario 1 (crea los FIFOs)\n");
        printf("2 = Usuario 2 (se conecta al chat)\n");
        exit(1);
    }

    if (strcmp(argv[1], "1") == 0) {
        mkfifo(FIFO1, 0666);
        mkfifo(FIFO2, 0666);
        chat(FIFO1, FIFO2);
    } else if (strcmp(argv[1], "2") == 0) {
        chat(FIFO2, FIFO1);
    } else {
        printf("Parámetro inválido. Usa 1 o 2.\n");
        exit(1);
    }

    return 0;
}
