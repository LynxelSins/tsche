#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>
#include "common/task_types.h"

int   pm_init(void);                       /* create log directory */
pid_t pm_spawn(const Task *t);             /* fork + exec, returns pid or -1 */
int   pm_wait(pid_t pid, int *exit_code, int *term_signal); /* blocks */
int   pm_signal_group(pid_t pid, int sig); /* signal the task's whole process group */

#endif
