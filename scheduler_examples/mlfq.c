#include "mlfq.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>
#include "debug.h"

static const uint32_t level_quantum_ms[MLFQ_LEVELS] = { 500, 1000, 2000 }; // Quantum de cada nível (0.5s, 1s, 2s)

typedef struct {
    uint32_t pid;   // ID do processo
    int level;      // nível de prioridade (0 = alta, 2 = baixa)
    uint32_t run_ms;// tempo já gasto neste nível
} meta_t;

static meta_t meta_tbl[MAX_META] = {0}; // tabela para armazenar info de todos os processos

// Função auxiliar: encontra ou cria entrada meta_t para um processo
static meta_t *m_find(uint32_t pid) {
    for (int i = 0; i < MAX_META; ++i) {
        if (meta_tbl[i].pid == pid) return &meta_tbl[i]; // se já existe, devolve
    }
    // se não existe, cria uma nova entrada livre
    for (int i = 0; i < MAX_META; ++i) {
        if (meta_tbl[i].pid == 0) {
            meta_tbl[i].pid = pid;
            meta_tbl[i].level = 0;
            meta_tbl[i].run_ms = 0;
            return &meta_tbl[i];
        }
    }
    return NULL; // tabela cheia
}

// Função auxiliar: remove o processo da tabela quando termina
static void m_remove(uint32_t pid) {
    for (int i = 0; i < MAX_META; ++i) {
        if (meta_tbl[i].pid == pid) {
            meta_tbl[i].pid = 0;
            meta_tbl[i].level = 0;
            meta_tbl[i].run_ms = 0;
            return;
        }
    }
}

// Função auxiliar: encontra o processo de maior prioridade na fila
static pcb_t* find_highest_priority(queue_t *rq, int *highest_level) {
    pcb_t *highest = NULL;
    *highest_level = MLFQ_LEVELS; // Inicializa com o pior nível possível

    queue_elem_t *elem = rq->head;
    while (elem != NULL) {
        pcb_t *p = elem->pcb;
        meta_t *m = m_find(p->pid);
        int level = (m != NULL) ? m->level : 0;

        if (level < *highest_level) {
            *highest_level = level;
            highest = p;
        }
        elem = elem->next;
    }

    return highest;
}

// Função auxiliar: remove um processo específico da fila
static void remove_pcb_from_queue(queue_t *rq, pcb_t *pcb) {
    if (rq->head == NULL) return;

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

// Escalonador MLFQ com suporte a múltiplas CPUs
void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpus, int num_cpus) {
    int i;

    // 1. Atualiza todos os processos que estão a correr nas CPUs
    for (i = 0; i < num_cpus; i++) {
        pcb_t *p = cpus[i];
        if (p == NULL) continue;

        // Atualiza tempo decorrido
        p->ellapsed_time_ms += TICKS_MS;

        // Atualiza tempo no nível atual
        meta_t *m = m_find(p->pid);
        if (m != NULL) {
            m->run_ms += TICKS_MS;
        }

        // Se o processo terminou
        if (p->ellapsed_time_ms >= p->time_ms) {
            DBG("Process %d finished CPU burst on CPU %d (MLFQ)\n", p->pid, i);

            msg_t msg = {
                .pid = p->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            write(p->sockfd, &msg, sizeof(msg_t));

            // Limpa metadados
            m_remove(p->pid);

            // Atualiza estado
            p->status = TASK_COMMAND;
            p->ellapsed_time_ms = 0;
            cpus[i] = NULL;
            continue;
        }

        // Se atingiu o quantum do nível atual
        if (m != NULL && m->run_ms >= level_quantum_ms[m->level]) {
            DBG("Process %d preempted on CPU %d (quantum expired in level %d)\n",
                p->pid, i, m->level);

            // Ajusta tempo restante
            p->time_ms -= p->ellapsed_time_ms;
            p->ellapsed_time_ms = 0;

            // Baixa de prioridade (se não estiver no último nível)
            if (m->level < MLFQ_LEVELS - 1) {
                m->level++;
            }
            m->run_ms = 0;

            // Volta para a fila
            p->status = TASK_RUNNING;
            enqueue_pcb(rq, p);
            cpus[i] = NULL;
        }
    }

    // 2. Aging: promove processos que esperam muito tempo
    static uint32_t last_aging_time = 0;
    if (current_time_ms - last_aging_time > 1000) { // Aging a cada 1 segundo
        queue_elem_t *elem = rq->head;
        while (elem != NULL) {
            pcb_t *p = elem->pcb;
            meta_t *m = m_find(p->pid);

            if (m != NULL && m->level > 0) {
                // Incrementa tempo de espera (simplificado)
                static uint32_t waiting_time[256] = {0};
                waiting_time[p->pid] += 1000;

                // Se esperou mais de 2 segundos, promove
                if (waiting_time[p->pid] > 2000) {
                    DBG("Process %d promoted from level %d due to aging\n",
                        p->pid, m->level);

                    m->level--;
                    m->run_ms = 0;
                    waiting_time[p->pid] = 0;
                }
            }
            elem = elem->next;
        }
        last_aging_time = current_time_ms;
    }

    // 3. Coloca processos da ready_queue nas CPUs livres
    for (i = 0; i < num_cpus; i++) {
        if (cpus[i] != NULL) continue; // CPU ocupada

        // Encontra o processo de maior prioridade (menor nível)
        int highest_level;
        pcb_t *highest = find_highest_priority(rq, &highest_level);

        if (highest == NULL) break; // Nada para executar

        // Remove da fila
        remove_pcb_from_queue(rq, highest);

        // Coloca na CPU
        highest->status = TASK_RUNNING;
        highest->ellapsed_time_ms = 0;
        cpus[i] = highest;

        // Atualiza metadados
        meta_t *m = m_find(highest->pid);
        if (m != NULL) {
            m->run_ms = 0; // Reinicia contador de quantum
        }

        DBG("Process %d started on CPU %d from level %d (MLFQ)\n",
            highest->pid, i, highest_level);
    }
}