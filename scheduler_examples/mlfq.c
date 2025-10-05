#include "mlfq.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

/* --- parâmetros e tabela de metadados (file-scope) --- */
#define MLFQ_LEVELS 3
static const uint32_t level_quantum_ms[MLFQ_LEVELS] = { 500, 1000, 2000 };
#define MAX_META 256
typedef struct { uint32_t pid; int level; uint32_t run_ms; } meta_t;
static meta_t meta_tbl[MAX_META] = {0};

/* --- helpers file-scope (static) --- */
static meta_t *m_find(uint32_t pid) {
    for (int i = 0; i < MAX_META; ++i) {
        if (meta_tbl[i].pid == pid) return &meta_tbl[i];
    }
    for (int i = 0; i < MAX_META; ++i) {
        if (meta_tbl[i].pid == 0) {
            meta_tbl[i].pid = pid;
            meta_tbl[i].level = 0;
            meta_tbl[i].run_ms = 0;
            return &meta_tbl[i];
        }
    }
    return NULL;
}

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

void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    /* --- avanço do trabalho da tarefa em CPU --- */
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;

        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            /* terminou: notifica e liberta */
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            if (write((*cpu_task)->sockfd, &msg, sizeof(msg_t)) != sizeof(msg_t)) {
                perror("write");
            }
            m_remove((*cpu_task)->pid);
            free(*cpu_task);
            *cpu_task = NULL;
        } else {
            /* não terminou: actualiza contador no nível e verifica quantum */
            meta_t *m = m_find((*cpu_task)->pid);
            if (m) {
                if (m->level < 0) m->level = 0;
                if (m->level >= MLFQ_LEVELS) m->level = MLFQ_LEVELS - 1;
                m->run_ms += TICKS_MS;
                if (m->run_ms >= level_quantum_ms[m->level]) {
                    /* atingiu quantum -> demote se possível, re-enqueue e liberta CPU */
                    if (m->level < MLFQ_LEVELS - 1) m->level++;
                    m->run_ms = 0;
                    enqueue_pcb(rq, *cpu_task);
                    *cpu_task = NULL;
                }
            }
            /* se m == NULL (tabela cheia) simplesmente não demotamos */
        }
    }

    /* --- se CPU livre, escolhe a tarefa com maior prioridade (menor level) --- */
    if (*cpu_task == NULL) {
        pcb_t *selected = NULL;
        pcb_t *curr;
        while ((curr = dequeue_pcb(rq)) != NULL) {
            meta_t *mc = m_find(curr->pid); /* cria meta se nova */
            int curr_level = mc ? mc->level : 0;
            if (selected == NULL) {
                selected = curr;
            } else {
                meta_t *ms = m_find(selected->pid);
                int sel_level = ms ? ms->level : 0;
                if (curr_level < sel_level) {
                    enqueue_pcb(rq, selected);
                    selected = curr;
                } else {
                    enqueue_pcb(rq, curr);
                }
            }
        }
        *cpu_task = selected;
    }
}