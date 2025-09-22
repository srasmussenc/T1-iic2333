#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// posibles estados de un proceso
typedef enum {
    NEW,
    READY,
    RUNNING,
    WAITING,
    FINISHED,
    DEAD
} ProcessState;

// Definición de proceso
struct Process {
    // Datos de entrada que vienen en el archivo
    char NOMBRE_PROCESO[64];
    int PID;
    int T_INICIO;
    int T_CPU_BURST;    // duración de un burst
    int N_BURSTS;  // cuántos bursts faltan
    int IO_WAIT;           // tiempo en WAITING entre bursts
    int T_DEADLINE;

    // Datos de simulación
    ProcessState state;
    int time_left_in_burst;   // cuánto le queda del burst actual
    int quantum_left;         // cuánto le queda de quantum
    int last_cpu_time;        // última vez que salió de CPU (para re-promoción)

    // Datos para estadísticas
    int first_response_time;  // primera vez que entró a CPU (-1 si no ha entrado)
    int end_time;             // tick en que terminó (FINISHED o DEAD)
    int waiting_time;         // acumulado en READY o WAITING
    int interruptions;        // veces interrumpido
};

#define MAX_PROCESSES 1024

// Cola circular para procesos
typedef struct {
    struct Process* items[MAX_PROCESSES];
    int front;
    int rear;
    int size;
} Queue;

// inicializa una cola vacía
void init_queue(Queue* q) {
    q->front = 0;
    q->rear = -1;
    q->size = 0;
}

// verifica si la cola está vacía
bool is_empty(Queue* q) {
    return q->size == 0;
}

// agrega un proceso al final de la cola
void enqueue(Queue* q, struct Process* p) {
    if (q->size < MAX_PROCESSES) {
        q->rear = (q->rear + 1) % MAX_PROCESSES;
        q->items[q->rear] = p;
        q->size++;
    } else {
        printf("Error: cola llena\n");
    }
}

// elimina y retorna el proceso al frente de la cola
struct Process* dequeue(Queue* q) {
    if (is_empty(q)) return NULL;
    struct Process* p = q->items[q->front];
    q->items[q->front] = NULL;
    q->front = (q->front + 1) % MAX_PROCESSES;
    q->size--;
    return p;
}

// retorna el proceso al frente de la cola sin eliminarlo
struct Process* peek(Queue* q) {
    if (is_empty(q)) return NULL;
    return q->items[q->front];
}

// Funciones auxiliares
struct Process* init_process(
    char* NOMBRE_PROCESO, int PID, int T_INICIO, int T_CPU_BURST,
    int N_BURSTS, int IO_WAIT, int T_DEADLINE
) {
    struct Process* p = malloc(sizeof(struct Process));
    strncpy(p->NOMBRE_PROCESO, NOMBRE_PROCESO, sizeof(p->NOMBRE_PROCESO) - 1);
    p->NOMBRE_PROCESO[sizeof(p->NOMBRE_PROCESO) - 1] = '\0'; //es el nombre de dicho proceso.

    p->PID = PID; //es el Process ID del proceso.
    p->T_INICIO = T_INICIO;
    p->T_CPU_BURST = T_CPU_BURST;
    p->N_BURSTS = N_BURSTS; //cantidad de rafagas de ejecución en la CPU que tiene el proceso. 
    p->IO_WAIT = IO_WAIT;
    p->T_DEADLINE = T_DEADLINE;

    // Estado inicial
    p->state = NEW;
    p->time_left_in_burst = T_CPU_BURST;
    p->quantum_left = 0;
    p->last_cpu_time = -1;

    // Estadísticas
    p->first_response_time = -1;
    p->end_time = -1;
    p->waiting_time = 0;
    p->interruptions = 0;

    return p;
}

void print_process(struct Process* p) {
    printf("PID=%d, Nombre=%s, start=%d, burst=%d, bursts=%d, io=%d, T_DEADLINE=%d\n",
           p->PID, p->NOMBRE_PROCESO, p->T_INICIO, p->T_CPU_BURST,
           p->N_BURSTS, p->IO_WAIT, p->T_DEADLINE);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s <archivo de entrada>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        perror("Error al abrir archivo");
        return 1;
    }

    // --- leer parámetros globales ---
    int q, K, N;
    fscanf(f, "%d", &q);  //q es el quantum asociado a cada cola
    fscanf(f, "%d", &K);  //K es la cantidad de procesos que se van a simular
    fscanf(f, "%d", &N);  //N es la cantidad de eventos

    printf("Parámetros globales: q=%d, K=%d, N=%d\n", q, K, N);

    // --- leer procesos ---
    struct Process* all_processes[MAX_PROCESSES];
    int total_processes = 0;

    char NOMBRE_PROCESO[64];
    int T_INICIO, T_CPU_BURST, N_BURSTS, IO_WAIT, extra, T_DEADLINE;

    for (int i = 0; i < K; i++) {
        if (fscanf(f, "%s %d %d %d %d %d %d",
                NOMBRE_PROCESO, &T_INICIO, &T_CPU_BURST,
                &N_BURSTS, &IO_WAIT, &extra, &T_DEADLINE) != 7) {
            printf("Error al leer proceso %d\n", i);
            break;
        }

        struct Process* p = init_process(
            NOMBRE_PROCESO, total_processes, T_INICIO,
            T_CPU_BURST, N_BURSTS, IO_WAIT, T_DEADLINE
        );
        all_processes[total_processes++] = p;
    }

    // --- leer eventos ---
    //ESTO FALTA IMPLEMENTARLO
    //los eventos son utilizados para ingresar inmediatamente al CPU un proceso con
    //cierto PID. Si existe un proceso en CPU distinto al indicado, este es interrumpido.
    //si el que está en el CPU es el mismo que el del evento, no pasa nada.
    //el proceso indicado en el evento debe ingresar al CPU o coontinuar su ejecución
    //independientemente de su estado actual (READY, WAITING, NEW)
    //el proceso interrumpido debe pasar a la cola High con máxima prioridad, ignorando
    //la formula de prioridad hasta que logre ingresar al CPU nuevamente.
    int pid_evento, tick_evento;
    fscanf(f, "%d %d", &pid_evento, &tick_evento); //ahora solo lee un evento, pero pueden haber más de uno

    fclose(f);

    printf("Pid evento: %d Tick donde ocurre: %d\n", pid_evento, tick_evento);

    // Imprimir procesos cargados
    printf("Procesos cargados (%d):\n", total_processes);
    for (int i = 0; i < total_processes; i++) {
        print_process(all_processes[i]);
    }

    // Inicializar colas
    Queue high, low;
    init_queue(&high);
    init_queue(&low);

    return 0;
}



