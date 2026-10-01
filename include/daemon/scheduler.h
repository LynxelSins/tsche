#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common/task_types.h"

int  sched_init(int num_workers);           /* start worker threads */
void sched_shutdown(void);                  /* stop, terminate running tasks, join */

int  sched_submit(const char *command, int priority); /* task id, or -1 */
int  sched_list(Task *out, int max);        /* snapshot; returns count */
int  sched_get(int id, Task *out);          /* 0 ok, -1 not found */
int  sched_kill(int id);                    /* 0 ok, -1 not found, -2 already finished */

#endif
