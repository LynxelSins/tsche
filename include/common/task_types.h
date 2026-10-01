#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <sys/types.h>
#include <time.h>

#define MAX_CMD_LEN      256
#define MAX_TASKS        1024
#define PRIORITY_MIN     0
#define PRIORITY_MAX     9
#define PRIORITY_DEFAULT 5

typedef enum {
    TASK_PENDING = 0,
    TASK_RUNNING,
    TASK_DONE,      /* exited with status 0              */
    TASK_FAILED,    /* exited with non-zero status       */
    TASK_KILLED     /* cancelled or terminated by signal */
} TaskStatus;

typedef struct {
    int        id;              /* 1-based, unique */
    char       command[MAX_CMD_LEN];
    int        priority;        /* 0..9, higher runs first */
    TaskStatus status;
    pid_t      pid;             /* 0 until started */
    int        exit_code;       /* valid when finished & term_signal == 0 */
    int        term_signal;     /* != 0 if killed by a signal */
    time_t     submit_time;
    time_t     start_time;
    time_t     end_time;
} Task;

const char *task_status_str(TaskStatus s);
int         task_is_finished(TaskStatus s);

#endif
