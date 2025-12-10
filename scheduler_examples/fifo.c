#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

#include "debug.h"

void fifo_scheduler(uint32_t current_time_ms,
                    queue_t *ready_queue,
                    pcb_t **cpus,
                    int num_cpus) {
    int i;

    // 1. Atualiza todos os processos que estão a correr nas CPUs
    for (i = 0; i < num_cpus; i++) {
        pcb_t *p = cpus[i];
        if (p == NULL) continue;

        p->ellapsed_time_ms += TICKS_MS;

        // Terminou o burst?
        if (p->ellapsed_time_ms >= p->time_ms) {
            DBG("Process %d finished CPU burst on CPU %d\n", p->pid, i);

            msg_t done_msg = {
                .pid = p->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            write(p->sockfd, &done_msg, sizeof(msg_t));

            p->status = TASK_COMMAND;
            p->ellapsed_time_ms = 0;
            cpus[i] = NULL;
        }
    }

    // 2. Coloca processos da ready_queue nas CPUs livres (FIFO)
    for (i = 0; i < num_cpus; i++) {
        if (cpus[i] != NULL) continue;           // CPU ocupada
        if (ready_queue->head == NULL) break;    // Nada para correr

        pcb_t *next = dequeue_pcb(ready_queue);
        next->status = TASK_RUNNING;
        next->ellapsed_time_ms = 0;
        cpus[i] = next;

        DBG("Process %d started on CPU %d (FIFO)\n", next->pid, i);
    }
}