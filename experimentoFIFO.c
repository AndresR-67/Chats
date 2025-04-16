#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#define MAX_PEDIDO_LEN 100
#define MAX_PEDIDOS 10

// Rutas de los FIFOs usados para la comunicación entre procesos
#define FIFO_CLIENTE "/tmp/fifo_cliente"
#define FIFO_COCINA "/tmp/fifo_cocina"
#define FIFO_MONITOR "/tmp/fifo_monitor"
#define FIFO_IDS "/tmp/fifo_ids"

// Estructura para representar un pedido
typedef struct {
    int cliente_id;
    char pedido[MAX_PEDIDO_LEN];
    int confirmado;
    int pedido_listo;
} Pedido;

Pedido lista_pedidos[MAX_PEDIDOS];
int total_pedidos = 0;
int id_actual = 1;

// Elimina los FIFOs al salir
void limpiar_fifos() {
    unlink(FIFO_CLIENTE);
    unlink(FIFO_COCINA);
    unlink(FIFO_MONITOR);
    unlink(FIFO_IDS);
    printf("FIFOs eliminados.\n");
}

// Maneja la interrupción Ctrl+C
void sigint_handler(int signo) {
    limpiar_fifos();
    exit(0);
}

// Proceso cocina
void cocina() {
    signal(SIGINT, sigint_handler);

    // Crea los FIFOs necesarios si no existen
    mkfifo(FIFO_CLIENTE, 0666);
    mkfifo(FIFO_COCINA, 0666);
    mkfifo(FIFO_MONITOR, 0666);

    // Abre FIFO de cliente para evitar bloqueo
    int dummy_write = open(FIFO_CLIENTE, O_WRONLY | O_NONBLOCK);

    // Abre los FIFOs correspondientes
    int fd_cliente = open(FIFO_CLIENTE, O_RDONLY);
    int fd_cocina = open(FIFO_COCINA, O_WRONLY);
    int fd_monitor = open(FIFO_MONITOR, O_WRONLY);

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        Pedido p;
        int leido = read(fd_cliente, &p, sizeof(Pedido));
        if (leido <= 0) continue;

        if (total_pedidos >= MAX_PEDIDOS) continue;

        // Confirma el pedido recibido
        printf("[Cocina] Preparando pedido del cliente %d: %s\n", p.cliente_id, p.pedido);
        p.confirmado = 1;
        write(fd_cocina, &p, sizeof(Pedido));

        // Simula el tiempo de preparación del pedido
        sleep(2);
        p.pedido_listo = 1;

        printf("[Cocina] Pedido de cliente %d listo.\n", p.cliente_id);
        write(fd_monitor, &p, sizeof(Pedido));
        write(fd_cocina, &p, sizeof(Pedido));

        // Actualiza el estado del pedido en la lista local
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (lista_pedidos[i].cliente_id == p.cliente_id && strcmp(lista_pedidos[i].pedido, p.pedido) == 0) {
                lista_pedidos[i] = p;
                break;
            }
        }
    }
}

// Proceso cliente
void cliente() {
    // Crea los FIFOs necesarios
    mkfifo(FIFO_CLIENTE, 0666);
    mkfifo(FIFO_COCINA, 0666);
    mkfifo(FIFO_MONITOR, 0666);
    mkfifo(FIFO_IDS, 0666);

    // Abre FIFO de cocina para evitar bloqueo
    int dummy_write = open(FIFO_COCINA, O_WRONLY | O_NONBLOCK);

    // Abre los FIFOs para lectura/escritura
    int fd_cliente = open(FIFO_CLIENTE, O_WRONLY);
    int fd_cocina = open(FIFO_COCINA, O_RDONLY);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);
    int fd_ids = open(FIFO_IDS, O_RDWR);

    // Solicita un nuevo ID al servidor de IDs
    int cliente_id;
    read(fd_ids, &cliente_id, sizeof(int));
    id_actual++;
    lseek(fd_ids, 0, SEEK_SET);
    write(fd_ids, &id_actual, sizeof(int));

    while (1) {
        char entrada[MAX_PEDIDO_LEN];
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", cliente_id);
        fgets(entrada, MAX_PEDIDO_LEN, stdin);

        // Salida si se presiona enter sin texto
        if (entrada[0] == '\n') break;

        // Elimina el salto de línea
        entrada[strcspn(entrada, "\n")] = 0;

        if (strlen(entrada) == 0) continue;

        // Construye el pedido
        Pedido p = {cliente_id, "", 0, 0};
        strncpy(p.pedido, entrada, MAX_PEDIDO_LEN);

        // Envia el pedido a la cocina
        write(fd_cliente, &p, sizeof(Pedido));

        // Espera confirmación
        read(fd_cocina, &p, sizeof(Pedido));
        if (p.confirmado) {
            printf("[Cliente %d] Pedido confirmado. Esperando preparacion... \n", cliente_id);
        }

        // Espera hasta que el pedido esté listo
        while (1) {
            Pedido respuesta;
            int leido = read(fd_cocina, &respuesta, sizeof(Pedido));
            if (leido > 0 &&
                respuesta.cliente_id == cliente_id &&
                strcmp(respuesta.pedido, entrada) == 0 &&
                respuesta.pedido_listo) {
                printf("[Cliente %d] Pedido preparado. ¡Buen provecho!\n", cliente_id);
                break;
            }
            sleep(1);
        }
    }

    // Cierre de descriptores
    close(fd_cliente);
    close(fd_cocina);
    close(fd_monitor);
    close(fd_ids);
}

// Proceso monitor
void monitor() {
    // Crea y abre el FIFO del monitor
    mkfifo(FIFO_MONITOR, 0666);
    int dummy_write = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);
    int fd_monitor = open(FIFO_MONITOR, O_RDONLY);

    while (1) {
        Pedido p;
        int leido = read(fd_monitor, &p, sizeof(Pedido));
        if (leido <= 0) continue;

        // Actualiza el estado del pedido si ya existía
        int actualizado = 0;
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (lista_pedidos[i].cliente_id == p.cliente_id &&
                strcmp(lista_pedidos[i].pedido, p.pedido) == 0) {
                lista_pedidos[i] = p;
                actualizado = 1;
                break;
            }
        }

        // Si es un nuevo pedido, lo agrega a la lista
        if (!actualizado && total_pedidos < MAX_PEDIDOS) {
            lista_pedidos[total_pedidos++] = p;
        }

        // Muestra el estado actualizado de todos los pedidos
        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");
        for (int i = 0; i < total_pedidos; i++) {
            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                i,
                lista_pedidos[i].cliente_id,
                lista_pedidos[i].pedido,
                lista_pedidos[i].confirmado ? "Sí" : "No",
                lista_pedidos[i].pedido_listo ? "Sí" : "No");
        }
        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        fflush(stdout);
        sleep(1);
    }

    close(fd_monitor);
}

// Proceso principal
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor|inicializar]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else if (strcmp(argv[1], "inicializar") == 0) {
        // Inicializa el FIFO de IDs con el valor 1
        mkfifo(FIFO_IDS, 0666);
        int fd = open(FIFO_IDS, O_WRONLY | O_CREAT);
        int inicial = 1;
        write(fd, &inicial, sizeof(int));
        close(fd);
        printf("FIFO_IDS inicializado con ID = 1\n");
    } else {
        printf("Argumento inválido. Usa cliente, cocina, monitor o inicializar.\n");
        return 1;
    }

    return 0;
}
