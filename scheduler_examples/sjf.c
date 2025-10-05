#include "sjf.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "msg.h"
#include "queue.h"


queue_elem_t* find_shortest_job(queue_t *rq) {
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

void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    // Se há uma tarefa executando na CPU
    if (*cpu_task != NULL) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        // Verifica se a tarefa atual terminou
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Tarefa finalizada - envia mensagem DONE para a aplicação
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };

            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }

            DBG("SJF: Process %d finished execution at time %d ms\n",
                (*cpu_task)->pid, current_time_ms);

            // Libera a PCB e marca CPU como ociosa
            free(*cpu_task);
            *cpu_task = NULL;
        }
    }

    // Se a CPU está ociosa e há tarefas na fila de pronto
    if (*cpu_task == NULL && rq->head != NULL) {
        // Encontra a tarefa com menor tempo de execução
        queue_elem_t *shortest_elem = find_shortest_job(rq);

        if (shortest_elem != NULL) {
            // Remove o elemento da fila de pronto
            remove_queue_elem(rq, shortest_elem);

            // Coloca a tarefa na CPU
            *cpu_task = shortest_elem->pcb;
            free(shortest_elem); // Libera apenas o elemento da fila, não a PCB

            DBG("SJF: Selected process %d with burst time %d ms at time %d ms\n",
                (*cpu_task)->pid, (*cpu_task)->time_ms, current_time_ms);
        }
    }
}