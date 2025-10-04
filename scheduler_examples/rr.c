#include "rr.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

#include "queue.h"

void rr_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    const uint32_t TIME_SLICE_MS = 500; /* 0.5s */

    /* Estado mínimo para RR (mantido entre chamadas) */
    static uint32_t rr_run_ms = 0;
    static pcb_t *rr_task_ptr = NULL;

    if (*cpu_task) {
        /* continua a somar o tempo de execução total (igual ao original) */
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            /* Task finished: notifica e liberta (igual ao original) */
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            free((*cpu_task));
            *cpu_task = NULL;

            /* reset estado RR porque CPU ficou livre */
            rr_run_ms = 0;
            rr_task_ptr = NULL;
        } else {
            /* tarefa não terminou: actualiza contador RR
               se mudou a tarefa (ponteiro diferente) reinicia contador */
            if (rr_task_ptr != *cpu_task) {
                rr_task_ptr = *cpu_task;
                rr_run_ms = 0;
            }
            rr_run_ms += TICKS_MS;

            /* se atingiu quantum, preempta: enfileira no fim e troca */
            if (rr_run_ms >= TIME_SLICE_MS) {
                /* move a tarefa corrente para o fim da fila */
                enqueue_pcb(rq, *cpu_task);
                *cpu_task = NULL;
                rr_run_ms = 0;
                rr_task_ptr = NULL;

                /* escolhe próxima tarefa (pode ser NULL se fila vazia) */
                *cpu_task = dequeue_pcb(rq);
                if (*cpu_task) {
                    rr_task_ptr = *cpu_task;
                    rr_run_ms = 0;
                }
            }
        }
    }

    /* Se CPU estiver livre, obtém próxima da ready-queue (FIFO) */
    if (*cpu_task == NULL) {
        *cpu_task = dequeue_pcb(rq);
        if (*cpu_task) {
            rr_task_ptr = *cpu_task;
            rr_run_ms = 0;
        }
    }
}