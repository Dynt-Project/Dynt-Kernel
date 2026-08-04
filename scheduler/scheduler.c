#include "scheduler.h"

#include "../mem/lib/memory.h"
#include "../mem/mm/kheap.h"
#include "../arch/x86_64/cpu/control_regs.h"

#define SCHED_DEFAULT_WEIGHT 1024
#define BORE_BURST_SHIFT 20
#define BORE_MAX_BURST_SCORE 39

static sched_task_t *runqueue;
static sched_task_t *current_task;
static uint64_t next_task_id;
static uint32_t task_count;

static uint32_t bore_score(uint64_t burst_runtime)
{
    uint32_t score = 0;

    while (burst_runtime > (1ULL << BORE_BURST_SHIFT) &&
           score < BORE_MAX_BURST_SCORE)
    {
        burst_runtime >>= 1;
        score++;
    }

    return score;
}

static uint64_t effective_vruntime(const sched_task_t *task)
{
    return task->vruntime + ((uint64_t)task->burst_score * 1000000ULL);
}

void scheduler_init(void)
{
    runqueue = 0;
    current_task = 0;
    next_task_id = 1;
    task_count = 0;
}

sched_task_t *scheduler_create_kernel_task(const char *name,
                                           void (*entry)(void),
                                           void *stack_top)
{
    sched_task_t *task = (sched_task_t *)kheap_alloc(sizeof(sched_task_t), 16);

    if (!task || !entry || !stack_top)
        return 0;

    task->id = next_task_id++;
    k_strncpy(task->name, name ? name : "kernel-task", sizeof(task->name));
    task->state = SCHED_TASK_READY;
    task->weight = SCHED_DEFAULT_WEIGHT;
    task->context.rsp = (uint64_t)stack_top;
    task->context.rip = (uint64_t)entry;
    task->context.rflags = 0x202;
    task->context.cr3 = read_cr3();
    task_count++;

    return task;
}

void scheduler_enqueue(sched_task_t *task)
{
    if (!task || task->state == SCHED_TASK_DEAD)
        return;

    task->state = SCHED_TASK_READY;
    task->next = runqueue;
    runqueue = task;
}

sched_task_t *scheduler_pick_next(void)
{
    sched_task_t *best = 0;
    sched_task_t *prev = 0;
    sched_task_t *best_prev = 0;

    for (sched_task_t *task = runqueue; task; task = task->next)
    {
        if (task->state != SCHED_TASK_READY)
        {
            prev = task;
            continue;
        }

        if (!best || effective_vruntime(task) < effective_vruntime(best))
        {
            best = task;
            best_prev = prev;
        }

        prev = task;
    }

    if (!best)
        return current_task;

    if (best_prev)
        best_prev->next = best->next;
    else
        runqueue = best->next;

    best->next = 0;
    best->state = SCHED_TASK_RUNNING;
    current_task = best;
    return best;
}

void scheduler_tick(uint64_t elapsed_ns)
{
    if (!current_task || current_task->state != SCHED_TASK_RUNNING)
        return;

    current_task->runtime_ns += elapsed_ns;
    current_task->burst_runtime += elapsed_ns;
    current_task->burst_score = bore_score(current_task->burst_runtime);

    uint32_t weight = current_task->weight ? current_task->weight : SCHED_DEFAULT_WEIGHT;
    current_task->vruntime += (elapsed_ns * SCHED_DEFAULT_WEIGHT) / weight;
}

uint32_t scheduler_task_count(void)
{
    return task_count;
}

sched_task_t *scheduler_current(void)
{
    return current_task;
}
