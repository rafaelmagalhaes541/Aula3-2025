#ifndef SJF_H
#define SJF_H

#include "queue.h"
#include <stdint.h>

/**
 * @brief SJF (Shortest Job First) scheduler com suporte a múltiplos CPUs
 *
 * @param current_time_ms Tempo atual da simulação em ms
 * @param ready_queue     Fila de processos prontos
 * @param cpus            Array de ponteiros para os processos em execução (um por CPU)
 * @param num_cpus        Número de CPUs disponíveis
 */
void sjf_scheduler(uint32_t current_time_ms,
                   queue_t *ready_queue,
                   pcb_t **cpus,
                   int num_cpus);

#endif // SJF_H
