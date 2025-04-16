#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>


#define MAX_PEDIDO_LEN 100 // Tamaño maximo del pedido

#define MAX_PEDIDOS 10 // Cantidad máxima de pedidos en el sistema

// Rutas de los FIFOs  para comunicación
#define FIFO_CLIENTE "/tmp/fifo_cliente"     // Cliente -> Cocina
#define FIFO_COCINA "/tmp/fifo_cocina"       // Cocina -> Cliente
#define FIFO_MONITOR "/tmp/fifo_monitor"     // Todos -> Monitor
#define FIFO_IDS "/tmp/fifo_ids"             // Asignación de IDs

// Colas de los pedido de manera individual 
typedef struct {
    int cliente_id;                         // Identifacion del cliente  único del cliente
    char pedido[MAX_PEDIDO_LEN];            // Pedido ( pizza, queso etc, los alimentos )
    int confirmado;                         // Indica si el pedido fue recibido por la cocuna 
    int pedido_listo;                       // Indica si el cliente recibe el pedido 
} Pedido;

// Lista global de pedidos usada por monitor y cocina
Pedido lista_pedidos[MAX_PEDIDOS];
int cantidad_pedidos = 0;
int siguiente_id = 1; // Usado solo por el generador de ID

// Limpieza de  los FIFOs del sistema al salir
void limpiar_fifos() {
    unlink(FIFO_CLIENTE);
    unlink(FIFO_COCINA);
    unlink(FIFO_MONITOR);
    unlink(FIFO_IDS);
    printf("FIFOs eliminados.\n");
}

//  para limpiar los FIFOs si el programa es interrumpido
void sigint_handler(int signo) {
    limpiar_fifos();
    exit(0);
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Uso: %s [cliente|cocina|monitor|id]\n", argv[0]);
        return 1;
    }

    // limpieza cuando se hace ctlr+c
    signal(SIGINT, sigint_handler);

    
    mkfifo(FIFO_CLIENTE, 0666);
    mkfifo(FIFO_COCINA, 0666);
    mkfifo(FIFO_MONITOR, 0666);
    mkfifo(FIFO_IDS, 0666);

    
    if (strcmp(argv[1], "cocina") == 0) {
        int fd_entrada = open(FIFO_CLIENTE, O_RDONLY);
        int fd_salida = open(FIFO_COCINA, O_WRONLY | O_NONBLOCK);
        int fd_monitor = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);

        Pedido pedido_recibido;
        while (read(fd_entrada, &pedido_recibido, sizeof(Pedido)) > 0) {
            pedido_recibido.confirmado = 1;

            printf("Preparando pedido del cliente %d: %s\n",
                   pedido_recibido.cliente_id, pedido_recibido.pedido);

            sleep(2);  // Simula tiempo de preparación

            pedido_recibido.pedido_listo = 1;
            printf("Pedido del cliente %d listo.\n", pedido_recibido.cliente_id);

            // Enviar actualización al monitor
            write(fd_monitor, &pedido_recibido, sizeof(Pedido));
            // Enviar respuesta al cliente
            write(fd_salida, &pedido_recibido, sizeof(Pedido));
        }

        close(fd_entrada);
        close(fd_salida);
        close(fd_monitor);
    }

    
    else if (strcmp(argv[1], "cliente") == 0) {
        int fd_pedido = open(FIFO_CLIENTE, O_WRONLY);
        int fd_respuesta = open(FIFO_COCINA, O_RDONLY);
        int fd_ids = open(FIFO_IDS, O_RDONLY);

        Pedido mi_pedido;

        //solicita id
        read(fd_ids, &mi_pedido.cliente_id, sizeof(int));

        while (1) {
            printf("Ingrese su pedido (o 'salir'): ");
            fgets(mi_pedido.pedido, MAX_PEDIDO_LEN, stdin);
            mi_pedido.pedido[strcspn(mi_pedido.pedido, "\n")] = '\0';

            if (strcmp(mi_pedido.pedido, "salir") == 0)
                break;

            mi_pedido.confirmado = 0;
            mi_pedido.pedido_listo = 0;

            write(fd_pedido, &mi_pedido, sizeof(Pedido));
            printf("Pedido recibido. Esperando confirmación...\n");

            // Espera la respuesta de cocina
            Pedido respuesta;
            read(fd_respuesta, &respuesta, sizeof(Pedido));

            if (respuesta.confirmado) {
                printf("Pedido preparado. ¡Buen provecho!\n");
            } else {
                printf("Error al preparar el pedido.\n");
            }
        }

        close(fd_pedido);
        close(fd_respuesta);
        close(fd_ids);
    }

        else if (strcmp(argv[1], "monitor") == 0) {
        int fd_monitor = open(FIFO_MONITOR, O_RDONLY);
        int dummy = open(FIFO_MONITOR, O_WRONLY | O_NONBLOCK);  // Para evitar bloqueo

        while (1) {
            Pedido nuevo_pedido;
            if (read(fd_monitor, &nuevo_pedido, sizeof(Pedido)) > 0) {
                // Actualiza la lista
                int actualizado = 0;
                for (int i = 0; i < cantidad_pedidos; i++) {
                    if (lista_pedidos[i].cliente_id == nuevo_pedido.cliente_id) {
                        lista_pedidos[i] = nuevo_pedido;
                        actualizado = 1;
                        break;
                    }
                }
                if (!actualizado && cantidad_pedidos < MAX_PEDIDOS) {
                    lista_pedidos[cantidad_pedidos++] = nuevo_pedido;
                }


                printf("\n--- Estado de pedidos ---\n");
                for (int i = 0; i < cantidad_pedidos; i++) {
                    printf("[%d] Cliente ID: %d | pedido '%s' | Recibido: %s | Preparado: %s\n",
                           i + 1,
                           lista_pedidos[i].cliente_id,
                           lista_pedidos[i].pedido,
                           lista_pedidos[i].confirmado ? "sí" : "no",
                           lista_pedidos[i].pedido_listo ? "sí" : "no");
                }
                printf("-------------------------\n");
            }
        }

        close(fd_monitor);
        close(dummy);
    }

    //generador de ID
    else if (strcmp(argv[1], "id") == 0) {
        int fd_ids = open(FIFO_IDS, O_WRONLY);
        while (1) {
            write(fd_ids, &siguiente_id, sizeof(int));
            siguiente_id++;
            sleep(1);
        }
        close(fd_ids);
    }

    else {
        printf("Opción no reconocida. Usa cliente, cocina, monitor o id.\n");
        return 1;
    }

    return 0;
}
