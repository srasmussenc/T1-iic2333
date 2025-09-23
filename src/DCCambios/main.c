#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PROCESSES 1024

// posibles estados de un proceso
typedef enum {
    NEW,
    READY,
    RUNNING,
    WAITING,
    FINISHED,
    DEAD
} ProcessState;

struct Event {
    int PID;
    int T_EVENTO;
};

struct Event events[MAX_PROCESSES];

// Definición de proceso
struct Process {
    char NOMBRE_PROCESO[64];
    int PID;
    int T_INICIO;
    int T_CPU_BURST;
    int N_BURSTS;
    int IO_WAIT;
    int T_DEADLINE;

    // Datos de simulación
    ProcessState state;
    int time_left_in_burst;
    int quantum_left;
    int last_cpu_time;

    // Para IO waiting
    int io_remaining;

    // Datos para estadísticas
    int first_response_time;
    int end_time;
    int waiting_time;
    int interruptions;

    int forced_high; // es 1 si debe entrar a high con prioridad máxima hasta que regrese al CPU
};

// Cola circular para procesos
typedef struct {
    struct Process* items[MAX_PROCESSES];
    int front;
    int rear;
    int size;
} Queue;

void init_queue(Queue* q) {
    q->front = 0;
    q->rear = -1;
    q->size = 0;
}

bool is_empty(Queue* q) {
    return q->size == 0;
}

void enqueue(Queue* q, struct Process* p) {
    if (q->size < MAX_PROCESSES) {
        q->rear = (q->rear + 1) % MAX_PROCESSES;
        q->items[q->rear] = p;
        q->size++;
    } else {
        fprintf(stderr, "Error: cola llena\n");
    }
}

// encolar al frente (prioridad máxima)
void enqueue_front(Queue* q, struct Process* p) {
    if (q->size < MAX_PROCESSES) {
        q->front = (q->front - 1 + MAX_PROCESSES) % MAX_PROCESSES;
        q->items[q->front] = p;
        q->size++;
    } else {
        fprintf(stderr, "Error: cola llena\n");
    }
}

struct Process* dequeue(Queue* q) {
    if (is_empty(q)) return NULL;
    struct Process* p = q->items[q->front];
    q->items[q->front] = NULL;
    q->front = (q->front + 1) % MAX_PROCESSES;
    q->size--;
    return p;
}

struct Process* peek(Queue* q) {
    if (is_empty(q)) return NULL;
    return q->items[q->front];
}

// eliminar un proceso arbitrario de la cola (si está presente)
bool remove_from_queue(Queue* q, struct Process* p) {
    if (is_empty(q)) return false;
    struct Process* tmp[MAX_PROCESSES];
    int cnt = 0;
    bool found = false;
    for (int i = 0; i < q->size; i++) {
        int pos = (q->front + i) % MAX_PROCESSES;
        if (q->items[pos] == p) {
            found = true;
            continue;
        }
        tmp[cnt++] = q->items[pos];
    }
    // reconstruir cola con tmp
    q->front = 0;
    q->rear = (cnt == 0) ? -1 : (cnt - 1);
    q->size = cnt;
    for (int i = 0; i < cnt; i++) q->items[i] = tmp[i];
    // limpiar resto (opcional)
    for (int i = cnt; i < MAX_PROCESSES; i++) q->items[i] = NULL;
    return found;
}

// inicializa un proceso
struct Process* init_process(
    char* NOMBRE_PROCESO, int PID, int T_INICIO, int T_CPU_BURST,
    int N_BURSTS, int IO_WAIT, int T_DEADLINE
) {
    struct Process* p = malloc(sizeof(struct Process));
    if (!p) return NULL;
    strncpy(p->NOMBRE_PROCESO, NOMBRE_PROCESO, sizeof(p->NOMBRE_PROCESO)-1);
    p->NOMBRE_PROCESO[sizeof(p->NOMBRE_PROCESO)-1] = '\0';

    p->PID = PID;
    p->T_INICIO = T_INICIO;
    p->T_CPU_BURST = T_CPU_BURST;
    p->N_BURSTS = N_BURSTS;
    p->IO_WAIT = IO_WAIT;
    p->T_DEADLINE = T_DEADLINE;

    p->state = NEW;
    p->time_left_in_burst = T_CPU_BURST;
    p->quantum_left = 0;
    p->last_cpu_time = -1;
    p->io_remaining = 0;

    p->first_response_time = -1;
    p->end_time = -1;
    p->waiting_time = 0;
    p->interruptions = 0;
    p->io_remaining = 0;
    p->forced_high = 0;

    return p;
}

void print_process(struct Process* p) {
    printf("PID=%d, Nombre=%s, start=%d, burst=%d, bursts=%d, io=%d, DEADLINE=%d\n",
           p->PID, p->NOMBRE_PROCESO, p->T_INICIO, p->T_CPU_BURST,
           p->N_BURSTS, p->IO_WAIT, p->T_DEADLINE);
}

void free_all_processes(struct Process* arr[], int total) {
    if (!arr) return;
    for (int i = 0; i < total; i++) {
        if (arr[i]) {
            free(arr[i]);
            arr[i] = NULL;
        }
    }
}

struct Process* find_process_by_pid(struct Process* arr[], int total, int pid) {
    for (int i = 0; i < total; i++) {
        if (arr[i] && arr[i]->PID == pid) return arr[i];
    }
    return NULL;
}

int cmp_events(const void* a, const void* b) {
    const struct Event* ea = a;
    const struct Event* eb = b;
    if (ea->T_EVENTO < eb->T_EVENTO) return -1;
    if (ea->T_EVENTO > eb->T_EVENTO) return 1;
    return ea->PID - eb->PID;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <archivo de entrada>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "r");
    if (!f) {
        perror("Error al abrir archivo");
        return 1;
    }

    // leer parámetros globales
    int q, K, N;
    if (fscanf(f, "%d", &q) != 1 ||
        fscanf(f, "%d", &K) != 1 ||
        fscanf(f, "%d", &N) != 1) {
        fprintf(stderr, "Error: formato inicial inválido (q, K, N)\n");
        fclose(f);
        return 1;
    }
    if (K < 0 || K > MAX_PROCESSES || N < 0 || N > MAX_PROCESSES) {
        fprintf(stderr, "Error: K o N fuera de rango (máx %d)\n", MAX_PROCESSES);
        fclose(f);
        return 1;
    }
    printf("Parámetros: q=%d, K=%d, N=%d\n", q, K, N);

    // leer procesos
    struct Process* all_processes[MAX_PROCESSES];
    for (int i = 0; i < MAX_PROCESSES; i++) all_processes[i] = NULL;
    int total_processes = 0;

    char NOMBRE_PROCESO[64];
    int PID, T_INICIO, T_CPU_BURST, N_BURSTS, IO_WAIT, T_DEADLINE;
    for (int i = 0; i < K; i++) {
        if (fscanf(f, "%63s %d %d %d %d %d %d",
                   NOMBRE_PROCESO, &PID, &T_INICIO, &T_CPU_BURST,
                   &N_BURSTS, &IO_WAIT, &T_DEADLINE) != 7) {
            fprintf(stderr, "Error al leer proceso %d\n", i);
            free_all_processes(all_processes, total_processes);
            fclose(f);
            return 1;
        }
        struct Process* p = init_process(NOMBRE_PROCESO, PID, T_INICIO, T_CPU_BURST, N_BURSTS, IO_WAIT, T_DEADLINE);
        if (!p) { fprintf(stderr, "malloc falló\n"); free_all_processes(all_processes, total_processes); fclose(f); return 1; }
        all_processes[total_processes++] = p;
    }

    // leer eventos
    for (int i = 0; i < N; i++) {
        if (fscanf(f, "%d %d", &events[i].PID, &events[i].T_EVENTO) != 2) {
            fprintf(stderr, "Error al leer evento %d\n", i);
            free_all_processes(all_processes, total_processes);
            fclose(f);
            return 1;
        }
    }
    qsort(events, N, sizeof(events[0]), cmp_events);
    fclose(f);

    // colas
    Queue high, low;
    init_queue(&high);
    init_queue(&low);

    // variables del loop
    int now = 0;
    int finished_count = 0;
    int event_idx = 0;
    struct Process* running = NULL;

    // imprimimos datos iniciales
    printf("Procesos cargados: %d\n", total_processes);
    for (int i = 0; i < total_processes; i++) print_process(all_processes[i]);

    // loop principal (Paso 1 + eventos)
    while (finished_count < total_processes) {
        // 0) manejar eventos en este tick (puede haber varios con el mismo T_EVENTO)
        while (event_idx < N && events[event_idx].T_EVENTO == now) {
            int ev_pid = events[event_idx].PID;
            struct Process* target = find_process_by_pid(all_processes, total_processes, ev_pid);

            if (target) {
                printf("[tick %d][EVENT] Se forzó el PID=%d (%s) a CPU\n", 
                    now, ev_pid, target->NOMBRE_PROCESO);

                // Si hay un proceso corriendo distinto, interrumpirlo
                if (running && running->PID != ev_pid) {
                    // interrumpir proceso actual
                    running->state = READY;
                    running->interruptions++;
                    running->forced_high = 1; // el próximo ciclo debe entrar a high con prioridad máxima
                    enqueue_front(&high, running); // lo ponemos al frente de high (prioridad máxima)
                    running = NULL;
                }

                // Forzar target al CPU: quitarlo de cualquier cola si está en ellas
                remove_from_queue(&high, target);
                remove_from_queue(&low, target);

                // Si target estaba en WAITING, descartamos IO remaining (se fuerza)
                if (target->state == WAITING) {
                    target->io_remaining = 0;
                }
                // Si target es NEW o READY, asegurarnos que tiene time_left inicializado
                if (target->state == NEW) target->time_left_in_burst = target->T_CPU_BURST;

                // Poner target como RUNNING (si no es el que ya estaba)
                if (!running) {
                    target->state = RUNNING;
                    running = target;
                    if (target->first_response_time == -1) target->first_response_time = now;
                    // asignamos quantum_left (para cuando lo implementes)
                    target->quantum_left = 2 * q; // high quantum por defecto
                } else {
                    // si running == target (ya lo estaba), no hacemos nada
                }
            }
            event_idx++;
        }

        // 1) mover procesos que terminan IO -> READY (si io_remaining llega a 0)
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == WAITING) {
                if (p->io_remaining > 0) {
                    p->io_remaining--;
                    if (p->io_remaining == 0) {
                        // listo para su próxima ráfaga
                        p->state = READY;
                        p->time_left_in_burst = p->T_CPU_BURST;

                        //esto nos asegura que el proceso interrumpido no pierda prioridad
                        if (p->forced_high) {
                            enqueue_front(&high, p); // entra a High con prioridad máxima
                            p->forced_high = 0; // ya no es necesario
                        } else {
                            enqueue(&high, p); // entra a High

                    }
                }
            }
        }
        }

        // 2) ingresar procesos que empiezan en este tick
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == NEW && p->T_INICIO == now) {
                p->state = READY;
                p->time_left_in_burst = p->T_CPU_BURST;
                enqueue(&high, p);
            }
        }

        // 3) si no hay proceso corriendo, elegir uno desde High -> Low
        if (!running) {
            if (!is_empty(&high)) {
                running = dequeue(&high);
                running->quantum_left = 2 * q; // high quantum
            } else if (!is_empty(&low)) {
                running = dequeue(&low);
                running->quantum_left = q; // low quantum
            }

            if (running) {
                running->state = RUNNING;
                if (running->first_response_time == -1) running->first_response_time = now;
            }
        }

        // 4) ejecutar 1 tick del proceso en CPU
        if (running) {
            running->time_left_in_burst--;
            running->quantum_left--; // no se usa para democión aún

            // opcion 1: si terminó la ráfaga actual
            if (running->time_left_in_burst <= 0) {
                running->N_BURSTS--;

                if (running->N_BURSTS <= 0) {
                    // proceso terminó completamente
                    running->state = FINISHED;
                    running->end_time = now + 1; // terminó al final del tick
                    finished_count++;
                } else {
                    // todavía quedan ráfagas -> va a WAITING por IO_WAIT
                    running->state = WAITING;
                    running->io_remaining = running->IO_WAIT;
                    running->interruptions++; // cuenta como interrupción
                }
                running = NULL; // CPU queda libre

            // opcion 2: se acabó quantum pero aun queda ráfaga
            } else if (running->quantum_left <= 0) {
                running->state = READY;
                running->interruptions++;
                enqueue(&low, running); // baja a Low
                running = NULL; // CPU queda libre       
            }
            // opcion 3: sigue corriendo (nada que hacer)
        }

        // 5) actualizar waiting_time (READY + WAITING)
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == READY) p->waiting_time++;
        }

        // --- DEBUG LOGS ---
        printf("[tick %d] ", now);
        if (running) {
            printf("RUNNING: %s(PID=%d, burst_left=%d, quantum_left=%d) ",
                running->NOMBRE_PROCESO, running->PID,
                running->time_left_in_burst, running->quantum_left);
        } else {
            printf("CPU libre ");
        }

        printf("| READY high=%d low=%d\n", high.size, low.size);

        // Mostrar procesos en High
        printf("   High queue: ");
        for (int i = 0; i < high.size; i++) {
            int idx = (high.front + i) % MAX_PROCESSES;
            struct Process* p = high.items[idx];
            if (p) printf("%s(PID=%d,q_left=%d) ", p->NOMBRE_PROCESO, p->PID, p->quantum_left);
        }
        printf("\n");

        // Mostrar procesos en Low
        printf("   Low queue: ");
        for (int i = 0; i < low.size; i++) {
            int idx = (low.front + i) % MAX_PROCESSES;
            struct Process* p = low.items[idx];
            if (p) printf("%s(PID=%d,q_left=%d) ", p->NOMBRE_PROCESO, p->PID, p->quantum_left);
        }
        printf("\n");
        // --- FIN DEBUG LOGS ---

        // 6) avanzar tiempo
        now++;
        if (now > 10 * 1000 * 1000) { // guard
            fprintf(stderr, "Timeout: many ticks, aborting\n");
            break;
        }
    } // end while

    // imprimir resumen simple
    printf("\nSimulación finalizada en tick %d\n", now);
    printf("Nombre,PID,Estado,Interrupts,Turnaround,Response,Waiting\n");
    for (int i = 0; i < total_processes; i++) {
        struct Process* p = all_processes[i];
        if (!p) continue;
        const char* estado = (p->state == FINISHED) ? "FINISHED" : (p->state==DEAD) ? "DEAD" : "OTHER";
        int turnaround = (p->end_time == -1) ? -1 : (p->end_time - p->T_INICIO);
        int response = (p->first_response_time == -1) ? -1 : (p->first_response_time - p->T_INICIO);
        printf("%s,%d,%s,%d,%d,%d,%d\n",
               p->NOMBRE_PROCESO, p->PID, estado, p->interruptions, turnaround, response, p->waiting_time);
    }

    // liberar memoria
    free_all_processes(all_processes, total_processes);

    return 0;
}






