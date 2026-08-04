#ifndef SCHEDULER_SCHEDULER_H
#define SCHEDULER_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#define SCHED_TASK_NAME_MAX 32

typedef enum sched_task_state
{
    SCHED_TASK_READY,
    SCHED_TASK_RUNNING,
    SCHED_TASK_SLEEPING,
    SCHED_TASK_DEAD,
} sched_task_state_t;

typedef struct sched_context
{
    uint64_t rsp;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
} sched_context_t;

typedef struct sched_task
{
    uint64_t id;
    char name[SCHED_TASK_NAME_MAX];
    sched_task_state_t state;
    uint64_t vruntime;
    uint64_t runtime_ns;
    uint64_t burst_runtime;
    uint32_t burst_score;
    uint32_t weight;
    sched_context_t context;
    struct sched_task *next;
} sched_task_t;

#ifdef __cplusplus
extern "C" {
#endif

void scheduler_init(void);
sched_task_t *scheduler_create_kernel_task(const char *name,
                                           void (*entry)(void),
                                           void *stack_top);
void scheduler_enqueue(sched_task_t *task);
sched_task_t *scheduler_pick_next(void);
void scheduler_tick(uint64_t elapsed_ns);
uint32_t scheduler_task_count(void);
sched_task_t *scheduler_current(void);

#ifdef __cplusplus
}
#endif

#endif // SCHEDULER_SCHEDULER_H
