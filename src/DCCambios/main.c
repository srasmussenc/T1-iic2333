#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_PROCESSES 1000

typedef enum { NEW, READY, RUNNING, WAITING, FINISHED, DEAD } ProcessState;

struct Event {
    int PID;
    int T_EVENTO;
};
struct Event events[MAX_PROCESSES];

// ------------------- Definición de proceso -------------------
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

    int forced_high; // es 1 si debe entrar a high con prioridad máxima
    int current_queue; // 0=CPU, 1=High, 2=Low
    int just_started; // Flag para indicar si el proceso recién entró al CPU
};

// ------------------- Cola circular -------------------
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

struct Process* dequeue(Queue* q) {
    if (is_empty(q)) return NULL;
    struct Process* p = q->items[q->front];
    q->items[q->front] = NULL;
    q->front = (q->front + 1) % MAX_PROCESSES;
    q->size--;
    return p;
}

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
    q->front = 0;
    q->rear = (cnt == 0) ? -1 : (cnt - 1);
    q->size = cnt;
    for (int i = 0; i < cnt; i++) q->items[i] = tmp[i];
    for (int i = cnt; i < MAX_PROCESSES; i++) q->items[i] = NULL;
    return found;
}

// ------------------- Prioridad -------------------
double calc_priority(struct Process* p, int now) {
    int bursts_restantes = p->N_BURSTS;
    int tuntil = p->T_DEADLINE - now;
    if (tuntil <= 0) tuntil = 1;
    return (1.0 / (double)tuntil) + bursts_restantes;
}

void enqueue_with_priority(Queue* q, struct Process* p, int which_queue, int now) {
    if (q->size >= MAX_PROCESSES) return;
    double new_priority = calc_priority(p, now);
    int insert_pos = q->size;
    for (int i = 0; i < q->size; i++) {
        int idx = (q->front + i) % MAX_PROCESSES;
        struct Process* other = q->items[idx];
        double other_priority = calc_priority(other, now);
        if (new_priority > other_priority ||
           (new_priority == other_priority && p->PID < other->PID)) {
            insert_pos = i;
            break;
        }
    }
    for (int j = q->size; j > insert_pos; j--) {
        int to = (q->front + j) % MAX_PROCESSES;
        int from = (q->front + j - 1 + MAX_PROCESSES) % MAX_PROCESSES;
        q->items[to] = q->items[from];
    }
    int pos = (q->front + insert_pos) % MAX_PROCESSES;
    q->items[pos] = p;
    q->rear = (q->front + q->size) % MAX_PROCESSES;
    q->size++;
    p->current_queue = which_queue;
}

// ------------------- Resort queue -------------------
void resort_queue(Queue* q, int which_queue, int now) {
    if (q->size == 0) return;
    struct Process* temp[MAX_PROCESSES];
    int cnt = 0;
    while (!is_empty(q)) {
        temp[cnt++] = dequeue(q);
    }
    for (int i = 0; i < cnt; i++) {
        enqueue_with_priority(q, temp[i], which_queue, now);
    }
}

// ------------------- Utilidades -------------------
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
    p->forced_high = 0;
    p->current_queue = 0;
    p->just_started = 0;

    return p;
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

int cmp_processes(const void* a, const void* b) {
    struct Process* pa = *(struct Process**)a;
    struct Process* pb = *(struct Process**)b;
    return pa->PID - pb->PID;
}

// ------------------- MAIN -------------------
int Q_GLOBAL = 0;

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

    int q, K, N;
    if (fscanf(f, "%d", &q) != 1 ||
        fscanf(f, "%d", &K) != 1 ||
        fscanf(f, "%d", &N) != 1) {
        fprintf(stderr, "Error: formato inicial inválido\n");
        fclose(f);
        return 1;
    }
    Q_GLOBAL = q; // guardar quantum global
    printf("Parámetros: q=%d, K=%d, N=%d\n", q, K, N);

    struct Process* all_processes[MAX_PROCESSES];
    for (int i = 0; i < MAX_PROCESSES; i++) all_processes[i] = NULL;
    int total_processes = 0;

    char NOMBRE_PROCESO[64];
    int PID, T_INICIO, T_CPU_BURST, N_BURSTS, IO_WAIT, T_DEADLINE;
    for (int i = 0; i < K; i++) {
        fscanf(f, "%63s %d %d %d %d %d %d",
               NOMBRE_PROCESO, &PID, &T_INICIO, &T_CPU_BURST,
               &N_BURSTS, &IO_WAIT, &T_DEADLINE);
        struct Process* p = init_process(NOMBRE_PROCESO, PID, T_INICIO, T_CPU_BURST, N_BURSTS, IO_WAIT, T_DEADLINE);
        all_processes[total_processes++] = p;
    }

    for (int i = 0; i < N; i++) {
        fscanf(f, "%d %d", &events[i].PID, &events[i].T_EVENTO);
    }
    qsort(events, N, sizeof(events[0]), cmp_events);
    fclose(f);

    Queue high, low;
    init_queue(&high);
    init_queue(&low);

    int now = 0;
    int finished_count = 0;
    int event_idx = 0;
    struct Process* running = NULL;
    int max_event_time = N > 0 ? events[N-1].T_EVENTO : 0;

    // ------------------- LOOP PRINCIPAL -------------------
    while (finished_count < total_processes || now <= max_event_time) {
        // procesos nuevos at beginning to enqueue, but select later
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == NEW && p->T_INICIO <= now) {
                printf("[tick %d] Nuevo proceso %s(PID=%d) entra a READY\n", now, p->NOMBRE_PROCESO, p->PID);
                p->state = READY;
                p->time_left_in_burst = p->T_CPU_BURST;
                enqueue_with_priority(&high, p, 1, now);
            }
        }

        // Actualizar waiting_time para procesos en READY y WAITING
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == READY || p->state == WAITING) {
                p->waiting_time++;
                printf("[tick %d] %s(PID=%d) waiting, waiting_time=%d\n",
                       now, p->NOMBRE_PROCESO, p->PID, p->waiting_time);
            }
        }

        // manejar eventos
        bool event_handled = false;
        while (event_idx < N && events[event_idx].T_EVENTO == now) {
            int ev_pid = events[event_idx].PID;
            struct Process* target = find_process_by_pid(all_processes, total_processes, ev_pid);
            if (target) {
                printf("[tick %d][EVENT] Se forzó %s(PID=%d) a CPU\n", now, target->NOMBRE_PROCESO, target->PID);

                if (running && running->PID != ev_pid) {
                    printf("[tick %d][EVENT] Interrumpiendo %s(PID=%d)\n", now, running->NOMBRE_PROCESO, running->PID);
                    running->state = READY;
                    running->interruptions++;
                    running->forced_high = 1;
                    enqueue_with_priority(&high, running, 1, now);
                    running = NULL;
                }
                remove_from_queue(&high, target);
                remove_from_queue(&low, target);
                if (target->state == WAITING) target->io_remaining = 0;
                if (target->state == NEW) target->time_left_in_burst = target->T_CPU_BURST;
                target->state = RUNNING;
                target->current_queue = 0;
                target->forced_high = 0;
                target->just_started = 1;
                running = target;
                if (target->first_response_time == -1) target->first_response_time = now;
                target->quantum_left = 2 * Q_GLOBAL;
                printf("[tick %d] %s(PID=%d) entra a CPU con burst=%d, quantum=%d\n",
                       now, target->NOMBRE_PROCESO, target->PID, target->time_left_in_burst, target->quantum_left);
                event_handled = true;
            }
            event_idx++;
        }

        // IO -> READY
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == WAITING) {
                if (p->io_remaining == 0) {
                    printf("[tick %d] %s(PID=%d) terminó IO, vuelve a READY\n", now, p->NOMBRE_PROCESO, p->PID);
                    p->state = READY;
                    p->time_left_in_burst = p->T_CPU_BURST;
                    if (p->forced_high) {
                        enqueue_with_priority(&high, p, 1, now);
                        p->forced_high = 0;
                    } else if (p->current_queue == 2) {
                        enqueue_with_priority(&low, p, 2, now);
                    } else {
                        enqueue_with_priority(&high, p, 1, now);
                    }
                } else {
                    p->io_remaining--;
                }
            }
        }

        // deadlines
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state != FINISHED && p->state != DEAD) {
                if (now >= p->T_DEADLINE) {
                    p->state = DEAD;
                    p->end_time = now + 1;
                    finished_count++;
                    remove_from_queue(&high, p);
                    remove_from_queue(&low, p);
                    if (running == p) running = NULL;
                    printf("[tick %d] DEADLINE: %s(PID=%d) murió\n", now, p->NOMBRE_PROCESO, p->PID);
                }
            }
        }

        // ejecutar un tick
        if (running) {
            printf("[tick %d] %s(PID=%d) ejecutando, burst antes=%d\n",
                   now, running->NOMBRE_PROCESO, running->PID, running->time_left_in_burst);
            if (running->just_started) {
                running->just_started = 0;
            } else {
                running->time_left_in_burst--;
                running->quantum_left--;
                // if (running->first_response_time == -1) running->first_response_time = now;
                printf("[tick %d] %s(PID=%d) burst después=%d, quantum=%d\n",
                       now, running->NOMBRE_PROCESO, running->PID, running->time_left_in_burst, running->quantum_left);
                if (running->time_left_in_burst <= 0) {
                    running->N_BURSTS--;
                    if (running->N_BURSTS <= 0) {
                        running->state = FINISHED;
                        running->end_time = now;
                        finished_count++;
                        printf("[tick %d] %s(PID=%d) FINALIZÓ proceso completo\n", now, running->NOMBRE_PROCESO, running->PID);
                    } else {
                        running->state = WAITING;
                        running->io_remaining = running->IO_WAIT;
                        running->interruptions++;
                        printf("[tick %d] %s(PID=%d) terminó ráfaga, pasa a WAITING\n", now, running->NOMBRE_PROCESO, running->PID);
                    }
                    running->last_cpu_time = now;
                    running = NULL;
                } else if (running->quantum_left <= 0) {
                    running->state = READY;
                    running->interruptions++;
                    running->last_cpu_time = now;
                    enqueue_with_priority(&low, running, 2, now);
                    running = NULL;
                }
            }
        }

        // promoción Low->High
        for (int i = 0; i < total_processes; i++) {
            struct Process* p = all_processes[i];
            if (!p) continue;
            if (p->state == READY && p->current_queue == 2) {
                if (2 * p->T_DEADLINE < (now - p->last_cpu_time)) {
                    printf("[tick %d] PROMOCIÓN: %s(PID=%d) sube de Low a High\n", now, p->NOMBRE_PROCESO, p->PID);
                    remove_from_queue(&low, p);
                    enqueue_with_priority(&high, p, 1, now);
                }
            }
        }

        // resort queues
        resort_queue(&high, 1, now);
        resort_queue(&low, 2, now);

        // elegir proceso
        // elegir proceso
        if (!running) {
            if (!is_empty(&high)) {
                running = dequeue(&high);
                running->quantum_left = 2 * Q_GLOBAL;
            } else if (!is_empty(&low)) {
                running = dequeue(&low);
                running->quantum_left = Q_GLOBAL;
            }
            if (running) {
                running->state = RUNNING;
                running->just_started = 1;
                running->forced_high = 0;

                // FIX: registrar FIRST RESPONSE al entrar al CPU
                if (running->first_response_time == -1) running->first_response_time = now;

                printf("[tick %d] %s(PID=%d) entra a CPU con burst=%d, quantum=%d\n",
                    now, running->NOMBRE_PROCESO, running->PID, running->time_left_in_burst, running->quantum_left);
            }
}


        // imprimir estado de colas cada tick
        printf("[tick %d] Estado: RUNNING=%s | High=%d | Low=%d\n",
               now, running ? running->NOMBRE_PROCESO : "ninguno", high.size, low.size);

        now++;
    }
    // ------------------- FIN SIMULACIÓN -------------------

    printf("\nSimulación finalizada en tick %d\n", now);
    qsort(all_processes, total_processes, sizeof(struct Process*), cmp_processes);
    printf("Nombre,PID,Estado,Interrupts,Turnaround,Response,Waiting\n");
    for (int i = 0; i < total_processes; i++) {
        struct Process* p = all_processes[i];
        if (!p) continue;
        const char* estado = (p->state == FINISHED) ? "FINISHED" :
                             (p->state == DEAD) ? "DEAD" : "OTHER";
        int turnaround = (p->end_time == -1) ? -1 : (p->end_time - p->T_INICIO);
        int response = (p->first_response_time == -1) ? -1 : (p->first_response_time - p->T_INICIO);
        printf("%s,%d,%s,%d,%d,%d,%d\n",
               p->NOMBRE_PROCESO, p->PID, estado, p->interruptions, turnaround, response, p->waiting_time);
    }

    free_all_processes(all_processes, total_processes);
    return 0;
}
