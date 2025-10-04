#include "sjf.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

#include "queue.h"


void sjf_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;      // Add to the running time of the application/task
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            // Task finished
            // Send msg to application
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            // Application finished and can be removed (this is FIFO after all)
            free((*cpu_task));
            (*cpu_task) = NULL;
        }
    }
    if (*cpu_task == NULL) {            // If CPU is idle
        /* Inline SJF: percorre a ready-queue, encontra o PCB com menor tempo restante
           (time_ms - ellapsed_time_ms) e re-enqueue os restantes para preservar ordem. */
        pcb_t *shortest = NULL;
        pcb_t *curr;
        while ((curr = dequeue_pcb(rq)) != NULL) {
            uint32_t remaining_curr = (curr->time_ms > curr->ellapsed_time_ms) ?
                                      (curr->time_ms - curr->ellapsed_time_ms) : 0;
            if (shortest == NULL) {
                shortest = curr;
            } else {
                uint32_t remaining_shortest = (shortest->time_ms > shortest->ellapsed_time_ms) ?
                                              (shortest->time_ms - shortest->ellapsed_time_ms) : 0;
                if (remaining_curr < remaining_shortest) {
                    /* curr é mais curto: re-enqueue o anterior shortest e guarda curr como shortest */
                    enqueue_pcb(rq, shortest);
                    shortest = curr;
                } else {
                    /* curr não é o mais curto, re-enqueue curr */
                    enqueue_pcb(rq, curr);
                }
            }
        }
        /* shortest é o PCB com menor tempo restante (ou NULL se a fila vazia). */
        *cpu_task = shortest;
    }
}
