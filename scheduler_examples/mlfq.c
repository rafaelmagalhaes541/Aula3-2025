#include "mlfq.h"

#include <stdio.h>
#include <stdlib.h>

#include "msg.h"
#include <unistd.h>

#define MLFQ_LEVELS 3
static const uint32_t level_quantum_ms[MLFQ_LEVELS] = { 500, 1000, 2000 }; // Quantum de cada nível (0.5s, 1s, 2s)
#define MAX_META 256

typedef struct {
    uint32_t pid;   // ID do processo
    int level;      // nível de prioridade (0 = alta, 2 = baixa)
    uint32_t run_ms;// tempo já gasto neste nível
} meta_t;

static meta_t meta_tbl[MAX_META] = {0}; // tabela para armazenar info de todos os processos

//Função auxiliar: encontra ou cria entrada meta_t para um processo
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

//Função auxiliar: remove o processo da tabela quando termina
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

//Escalonador MLFQ
void mlfq_scheduler(uint32_t current_time_ms, queue_t *rq, pcb_t **cpu_task) {
    // Vetores simples para guardar nível e tempo de CPU por PID
    static int nivel[256] = {0};
    static uint32_t tempo_exec[256] = {0};
    const uint32_t quantum[3] = {500, 1000, 2000}; // tamanhos de fatia de tempo (time-slice)

    //Atualiza tarefa atual (se houver)
    if (*cpu_task) {
        (*cpu_task)->ellapsed_time_ms += TICKS_MS;
        tempo_exec[(*cpu_task)->pid] += TICKS_MS;

        // Se o processo terminou
        if ((*cpu_task)->ellapsed_time_ms >= (*cpu_task)->time_ms) {
            msg_t msg = {
                .pid = (*cpu_task)->pid,
                .request = PROCESS_REQUEST_DONE,
                .time_ms = current_time_ms
            };
            write((*cpu_task)->sockfd, &msg, sizeof(msg_t));
            nivel[(*cpu_task)->pid] = 0;
            tempo_exec[(*cpu_task)->pid] = 0;
            free(*cpu_task);
            *cpu_task = NULL;
        }
        // Se atingiu o quantum baixa prioridade e volta à fila
        else if (tempo_exec[(*cpu_task)->pid] >= quantum[nivel[(*cpu_task)->pid]]) {
            if (nivel[(*cpu_task)->pid] < 2)
                nivel[(*cpu_task)->pid]++;
            tempo_exec[(*cpu_task)->pid] = 0;
            enqueue_pcb(rq, *cpu_task);
            *cpu_task = NULL;
        }
    }

    // Se CPU está livre, escolher próxima tarefa
    if (*cpu_task == NULL) {
        pcb_t *melhor = NULL;
        int nivel_melhor = 3;
        int n = rq->size;

        // percorre todos os processos e escolhe o de menor nível
        for (int i = 0; i < n; i++) {
            pcb_t *p = dequeue_pcb(rq);
            int nv = nivel[p->pid];

            if (nv < nivel_melhor) {
                if (melhor) enqueue_pcb(rq, melhor);
                melhor = p;
                nivel_melhor = nv;
            } else enqueue_pcb(rq, p);
        }

        *cpu_task = melhor; // executa o processo com prioridade mais alta
    }
}