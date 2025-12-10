#ifndef RR_H
#define RR_H

#include <stdint.h>
#include "queue.h"

#define QUANTUM_MS 100

/**
 * @brief Round-Robin scheduler com suporte a múltiplos CPUs
 *
 * @param current_time_ms Tempo atual da simulação em ms
 * @param ready_queue     Fila de processos prontos
 * @param cpus            Array de ponteiros para os processos em execução (um por CPU)
 * @param num_cpus        Número de CPUs disponíveis
 */
void rr_scheduler(uint32_t current_time_ms,
                  queue_t *ready_queue,
                  pcb_t **cpus,
                  int num_cpus);

#endif // RR_H
