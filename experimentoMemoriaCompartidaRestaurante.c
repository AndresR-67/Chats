#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>


#define MAX_PEDIDOS 10                  // Número máximo de pedidos en cola
#define MAX_PEDIDO_LEN 100             // Longitud máxima del texto del pedido
#define SHM_NAME "/cola_pedidos"       // Nombre de la memoria compartida
#define SEM_MUTEX "/sem_mutex"         // Semáforo para exclusión mutua (control de acceso)
#define SEM_PEDIDOS "/sem_pedidos"     // Semáforo para avisar a la cocina de nuevos pedidos
#define SEM_CONFIRMACION "/sem_conf"   // Semáforo para avisar al cliente que su pedido fue recibido
#define SEM_IDS "/sem_ids"             // Semáforo para controlar el acceso al generador de ID

//  Cola de los pedidos de manera individual
typedef struct {
    int cliente_id;                        // Identificacion del cliente
    char texto_pedido[MAX_PEDIDO_LEN];     // Pedido ( pizza, queso etc los alimentos )
    int fue_recibido;                      // Indica si el pedido fue recibido por la cocina 
    int esta_listo;                        // Marca si el cliente recibio el pedido 
} Pedido;

// Cola circular de  los pedidos de manera general ( Se utiliza para gestionar los pedidos de una forma general )
typedef struct {
    Pedido pedidos[MAX_PEDIDOS];           // Arreglo circular de pedidos
    int indice_inicio;                     // Índice del primer pedido a procesar
    int indice_fin;                        // Índice donde se agregará el siguiente pedido
    int total_en_cola;                     // Número actual de pedidos en cola
    int siguiente_id;                      // Generador de ID únicos para clientes
} ColaPedidos;

// Inicializa la cola de pedidos en la memoria compartida
void inicializar_cola(ColaPedidos *cola) {
    cola->indice_inicio = 0;
    cola->indice_fin = 0;
    cola->total_en_cola = 0;
    cola->siguiente_id = 1;

    for (int i = 0; i < MAX_PEDIDOS; i++) {  
        cola->pedidos[i].cliente_id = -1;
        memset(cola->pedidos[i].texto_pedido, 0, MAX_PEDIDO_LEN);
        cola->pedidos[i].fue_recibido = 0;
        cola->pedidos[i].esta_listo = 0;
    }
}

// Proceso que sirve para ver el estado de la cola en tiempo real
void monitor() {
    int shm_fd;
    ColaPedidos *cola;

    // Shm_open abre  la memoria compartida en modo lectura/escritura
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("No se pudo abrir la memoria compartida");
        exit(1);
    }

    // Mapeao de la memoria compartida al proceso
    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    while (1) {
        system("clear");
        printf("\n--- ESTADO DE LA COLA DE PEDIDOS ---\n\n");

        // Ciclo que recorre los pedidos 
        for (int i = 0; i < MAX_PEDIDOS; i++) {
            if (cola->pedidos[i].cliente_id == -1 || strlen(cola->pedidos[i].texto_pedido) == 0)
                continue;

            printf("[%d] Cliente ID: %d | Pedido: %s | Recibido: %s | Preparado: %s\n",
                   i,
                   cola->pedidos[i].cliente_id,
                   cola->pedidos[i].texto_pedido,
                   cola->pedidos[i].fue_recibido ? "Sí" : "No",
                   cola->pedidos[i].esta_listo ? "Sí" : "No");
        }

        printf("\nPresiona Ctrl+C para salir del monitor.\n");
        sleep(1);  // Pausa durante 1 segundo para actualizar  la pantalla 
    }

  
}

// Procesos que recibe el pedido de los clientes 
void cocina() {
    int shm_fd;
    ColaPedidos *cola;

    // Semáforos
    sem_t *mutex, *sem_nuevo_pedido, *sem_confirmacion, *sem_ids;

    // evitar duplicados  
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_PEDIDOS);
    sem_unlink(SEM_CONFIRMACION);
    sem_unlink(SEM_IDS);

    // Creacion y configuracion de la memoria compartida
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(ColaPedidos)); // Reserva del espacio
    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    inicializar_cola(cola); 

    // Crear semáforos
    mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);          // Exclusión mutua
    sem_nuevo_pedido = sem_open(SEM_PEDIDOS, O_CREAT, 0666, 0); // Pedido pendiente
    sem_confirmacion = sem_open(SEM_CONFIRMACION, O_CREAT, 0666, 0); // Confirmación al cliente
    sem_ids = sem_open(SEM_IDS, O_CREAT, 0666, 1);          // Control de identificacion o ID

    printf("[Cocina] Esperando pedidos...\n");

    while (1) {
        
        sem_wait(sem_nuevo_pedido);

        // Se usa el  mutex para acceder a la cola
        sem_wait(mutex);

        if (cola->total_en_cola > 0) {
            Pedido *p = &cola->pedidos[cola->indice_inicio];

            printf("[Cocina] Preparando pedido del cliente %d: %s\n", p->cliente_id, p->texto_pedido);

            p->fue_recibido = 1;          // Marca recibido recibido el pedido
            sem_post(sem_confirmacion);  // aviso para el cliente

            sleep(2); // Espera de 2 segundos para simular el proceso de preparacion

            p->esta_listo = 1;           // Marca como listo el pedido
            printf("[Cocina] Pedido de cliente %d listo.\n", p->cliente_id);

            //  Se avanza en la cola circular 
            cola->indice_inicio = (cola->indice_inicio + 1) % MAX_PEDIDOS;
            cola->total_en_cola--;
        }

        // Se libera el acceso de la cola
        sem_post(mutex);
    }
}

// Proceso cliente
void cliente() {
    int shm_fd;
    ColaPedidos *cola;
    sem_t *mutex, *sem_nuevo_pedido, *sem_confirmacion, *sem_ids;

    // Conexion con el semaforo y con la memoria compartida
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    cola = mmap(0, sizeof(ColaPedidos), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

    mutex = sem_open(SEM_MUTEX, 0);
    sem_nuevo_pedido = sem_open(SEM_PEDIDOS, 0);
    sem_confirmacion = sem_open(SEM_CONFIRMACION, 0);
    sem_ids = sem_open(SEM_IDS, 0);

    int id;
    char texto[MAX_PEDIDO_LEN];

    // Generador de ID unico, protegido con el semaforo
    sem_wait(sem_ids);
    id = cola->siguiente_id++;
    sem_post(sem_ids);

    while (1) {
        printf("[Cliente %d] Ingrese su pedido (ENTER para salir): ", id);
        fgets(texto, MAX_PEDIDO_LEN, stdin);

        if (texto[0] == '\n') break; // Salir con ENTER vacío

        texto[strcspn(texto, "\n")] = 0; // Eliminar salto de línea

        if (strlen(texto) == 0) continue;

        sem_wait(mutex); // Exclusion mutua para acceder a la cola

        if (cola->total_en_cola == MAX_PEDIDOS) {
            // Cola llena
            printf("[Cliente %d] Cola llena. Intente más tarde.\n", id);
            sem_post(mutex);
            continue;
        }

        // Colocar pedido al final de la cola
        Pedido *nuevo = &cola->pedidos[cola->indice_fin];
        nuevo->cliente_id = id;
        strncpy(nuevo->texto_pedido, texto, MAX_PEDIDO_LEN);
        nuevo->fue_recibido = 0;
        nuevo->esta_listo = 0;

        // Actualizacion del indice y del contador
        cola->indice_fin = (cola->indice_fin + 1) % MAX_PEDIDOS;
        cola->total_en_cola++;

        sem_post(mutex);             // Liberacion del  acceso a la cola
        sem_post(sem_nuevo_pedido);  // Aviso a la cocina de que hay un nuevo pedido

    
        sem_wait(sem_confirmacion); //Espera para que la cocina confirme que lo recibio
        printf("[Cliente %d] Pedido recibido. Esperando preparación...\n", id);

        //Espera de los pedidos
        while (1) {
            sem_wait(mutex);
            int listo = 0;
            for (int i = 0; i < MAX_PEDIDOS; i++) {
                if (cola->pedidos[i].cliente_id == id && cola->pedidos[i].esta_listo) {
                    listo = 1;
                    break;
                }
            }
            sem_post(mutex);
            if (listo) break;
            sleep(1); // Espera antes de volver a revisar 
        }

        printf("[Cliente %d] Pedido preparado. ¡Buen provecho!\n", id);
    }

    // Liberación de recursos
    munmap(cola, sizeof(ColaPedidos));
    close(shm_fd);
    sem_close(mutex);
    sem_close(sem_nuevo_pedido);
    sem_close(sem_confirmacion);
    sem_close(sem_ids);
}

// Función principal
int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s [cliente|cocina|monitor]\n", argv[0]);
        return 1;
    }

    
    if (strcmp(argv[1], "cliente") == 0) {
        cliente();
    } else if (strcmp(argv[1], "cocina") == 0) {
        cocina();
    } else if (strcmp(argv[1], "monitor") == 0) {
        monitor();
    } else {
        printf("Argumento inválido. Use cliente, cocina o monitor.\n");
        return 1;
    }

    return 0;
}
