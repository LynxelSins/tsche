#include "common/task_types.h"

const char *task_status_str(TaskStatus s)
{
    switch (s) {
    case TASK_PENDING: return "PENDING";
    case TASK_RUNNING: return "RUNNING";
    case TASK_DONE:    return "DONE";
    case TASK_FAILED:  return "FAILED";
    case TASK_KILLED:  return "KILLED";
    }
    return "?";
}

int task_is_finished(TaskStatus s)
{
    return s == TASK_DONE || s == TASK_FAILED || s == TASK_KILLED;
}
