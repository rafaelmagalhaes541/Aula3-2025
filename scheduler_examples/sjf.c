#include "sjf.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "msg.h"
#include "queue.h"

// Função auxiliar: encontra o elemento com o job mais curto na fila
static queue_elem_t* find_shortest_job_elem(queue_t *rq) {
    if (rq == NULL || rq->head == NULL) {
        return NULL;
    }

    queue_elem_t *shortest_elem = rq->head;
    queue_elem_t *current_elem = rq->head->next;
    uint32_t shortest_time = shortest_elem->pcb->time_ms;

    // Percorre a fila para encontrar o trabalho mais curto
    while (current_elem != NULL) {
        if (current_elem->pcb->time_ms < shortest_time) {
            shortest_elem = current_elem;
            shortest_time = current_elem->pcb->time_ms;
        }
        current_elem = current_elem->next;
    }

    return shortest_elem;
}

// Função auxiliar: encontra e remove o job mais curto da fila
static pcb_t* dequeue_shortest_job(queue_t *rq) {
    queue_elem_t *shortest_elem = find_shortest_job_elem(rq);
    if (shortest_elem == NULL) {
        return NULL;
    }

    pcb_t *shortest_pcb = shortest_elem->pcb;
    remove_queue_elem(rq, shortest_elem);
    free(shortest_elem);

    return shortest_pcb;
}

// Função auxiliar: encontra o processo mais curto na fila (para preempção)
static pcb_t* find_shortest_in_queue(queue_t *rq, uint32_t *shortest_time) {
    if (rq->head == NULL) {
        *shortest_time = UINT32_MAX;
        return NULL;
    }

    queue_elem_t *elem = rq->head;
    pcb_t *shortest_pcb = NULL;
    uint32_t min_time = UINT32_MAX;

    while (elem != NULL) {
        if (elem->pcb->time_ms < min_time) {
            min_time = elem->pcb->time_ms;
            shortest_pcb = elem->pcb;
        }
        elem = elem->next;
    }

    *shortest_time = min_time;
    return shortest_pcb;
}

// Função auxiliar: remove um PCB específico da fila
static void remove_specific_pcb(queue_t *rq, pcb_t *pcb) {
    if (rq->head == NULL || pcb == NULL) return;

    // Caso especial: primeiro elemento
    if (rq->head->pcb == pcb) {
        queue_elem_t *to_remove = rq->head;
        rq->head = rq->head->next;
        if (rq->head == NULL) {
            rq->tail = NULL;
        }
        free(to_remove);
        return;
    }

    // Procura o elemento
    queue_elem_t *prev = rq->head;
    queue_elem_t *current = rq->head->next;

    while (current != NULL) {
        if (current->pcb == pcb) {
            prev->next = current->next;
            if (current == rq->tail) {
                rq->tail = prev;
            }
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

// SJF com suporte a múltiplas CPUs e preempção
void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpus, int num_cpus) {
    int i;

    // 1. Atualiza todos os processos que estão a correr nas CPUs
    for (i = 0; i < num_cpus; i++) {
        pcb_t *p = cpus[i];
        if (p == NULL) continue;

        p->ellapsed_time_ms += TICKS_MS;

        // Verifica se a tarefa atual terminou
        if (p->ellapsed_time_ms >= p->time_ms) {
            DBG("SJF: Process %d finished execution on CPU %d at time %d ms\n",
                p->pid, i, current_time_ms);

            // Tarefa finalizada - envia mensagem DONE para a aplicação
            msg_t msg = {
                .pid = p->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };

            if (write(p->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            // Atualiza estado
            p->status = TASK_COMMAND;
            p->ellapsed_time_ms = 0;
            cpus[i] = NULL;
        }
    }

    // 2. Verificar preempção (SJF preemptivo)
    // Em SJF preemptivo, se um processo mais curto chegar à ready queue,
    // ele pode preemptar um processo mais longo que está a executar
    for (i = 0; i < num_cpus; i++) {
        pcb_t *current = cpus[i];
        if (current == NULL) continue;

        // Calcula tempo restante do processo atual
        uint32_t remaining_time = current->time_ms - current->ellapsed_time_ms;

        // Encontra o processo mais curto na ready queue
        uint32_t shortest_in_queue_time;
        pcb_t *shortest_in_queue = find_shortest_in_queue(rq, &shortest_in_queue_time);

        // Se houver um processo mais curto na fila que o tempo restante do atual
        if (shortest_in_queue != NULL && shortest_in_queue_time < remaining_time) {
            DBG("SJF: Process %d preempted by shorter process %d on CPU %d\n",
                current->pid, shortest_in_queue->pid, i);

            // Remove o processo mais curto da fila
            remove_specific_pcb(rq, shortest_in_queue);

            // Coloca o processo atual de volta na fila
            current->time_ms = remaining_time; // Atualiza tempo restante
            current->ellapsed_time_ms = 0;
            current->status = TASK_RUNNING;
            enqueue_pcb(rq, current);

            // Coloca o processo mais curto na CPU
            shortest_in_queue->status = TASK_RUNNING;
            shortest_in_queue->ellapsed_time_ms = 0;
            cpus[i] = shortest_in_queue;

            DBG("SJF: Process %d started on CPU %d (preempted %d)\n",
                shortest_in_queue->pid, i, current->pid);
        }
    }

    // 3. Preencher CPUs livres com jobs mais curtos
    for (i = 0; i < num_cpus; i++) {
        if (cpus[i] != NULL) continue; // CPU ocupada

        if (rq->head == NULL) break; // Nada para executar

        // Seleciona o job mais curto
        pcb_t *shortest_job = dequeue_shortest_job(rq);
        if (shortest_job == NULL) break;

        // Coloca na CPU
        shortest_job->status = TASK_RUNNING;
        shortest_job->ellapsed_time_ms = 0;
        cpus[i] = shortest_job;

        DBG("SJF: Selected process %d with burst time %d ms on CPU %d at time %d ms\n",
            shortest_job->pid, shortest_job->time_ms, i, current_time_ms);
    }
}