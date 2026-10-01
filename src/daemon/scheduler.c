#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "daemon/process_manager.h"
#include "daemon/scheduler.h"

#define MAX_WORKERS 16

/* All state below is protected by g_lock. */
static Task            g_tasks[MAX_TASKS];
static int             g_count;          /* tasks[0..g_count-1] used; id = index+1 */
static int             g_stop;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;

static pthread_t g_workers[MAX_WORKERS];
static int       g_nworkers;

/* Highest priority first; ties -> lowest id (FIFO). Caller holds g_lock. */
static int pick_pending(void)
{
    int best = -1;
    for (int i = 0; i < g_count; i++) {
        if (g_tasks[i].status != TASK_PENDING) continue;
        if (best < 0 || g_tasks[i].priority > g_tasks[best].priority)
            best = i;
    }
    return best;
}

static void *worker_main(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_lock);

    while (!g_stop) {
        int idx = pick_pending();
        if (idx < 0) {
            pthread_cond_wait(&g_cond, &g_lock);
            continue;
        }

        Task *t = &g_tasks[idx];
        t->status = TASK_RUNNING;
        t->start_time = time(NULL);

        /* fork() while holding the lock is fine (fast), and it guarantees
         * nobody sees RUNNING with pid == 0. */
        pid_t pid = pm_spawn(t);
        if (pid < 0) {
            t->status = TASK_FAILED;
            t->exit_code = -1;
            t->end_time = time(NULL);
            continue;
        }
        t->pid = pid;

        pthread_mutex_unlock(&g_lock);
        int ec = 0, sig = 0;
        int rc = pm_wait(pid, &ec, &sig);          /* block WITHOUT the lock */
        pthread_mutex_lock(&g_lock);

        t->end_time = time(NULL);
        t->exit_code = ec;
        t->term_signal = sig;
        if (rc < 0)      t->status = TASK_FAILED;
        else if (sig)    t->status = TASK_KILLED;
        else if (ec)     t->status = TASK_FAILED;
        else             t->status = TASK_DONE;
    }

    pthread_mutex_unlock(&g_lock);
    return NULL;
}

int sched_init(int num_workers)
{
    if (num_workers < 1) num_workers = 1;
    if (num_workers > MAX_WORKERS) num_workers = MAX_WORKERS;

    for (int i = 0; i < num_workers; i++) {
        if (pthread_create(&g_workers[i], NULL, worker_main, NULL) != 0) {
            perror("pthread_create");
            break;
        }
        g_nworkers++;
    }
    return g_nworkers > 0 ? 0 : -1;
}

static int running_count(void)
{
    int n = 0;
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_count; i++)
        if (g_tasks[i].status == TASK_RUNNING) n++;
    pthread_mutex_unlock(&g_lock);
    return n;
}

static void signal_running(int sig)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < g_count; i++)
        if (g_tasks[i].status == TASK_RUNNING)
            pm_signal_group(g_tasks[i].pid, sig);
    pthread_mutex_unlock(&g_lock);
}

void sched_shutdown(void)
{
    pthread_mutex_lock(&g_lock);
    g_stop = 1;
    for (int i = 0; i < g_count; i++) {
        if (g_tasks[i].status == TASK_PENDING) {
            g_tasks[i].status = TASK_KILLED;
            g_tasks[i].end_time = time(NULL);
        }
    }
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);

    /* polite SIGTERM, then SIGKILL after ~2 s */
    signal_running(SIGTERM);
    for (int i = 0; i < 20 && running_count() > 0; i++)
        usleep(100 * 1000);
    signal_running(SIGKILL);

    for (int i = 0; i < g_nworkers; i++)
        pthread_join(g_workers[i], NULL);
}

int sched_submit(const char *command, int priority)
{
    if (!command || !*command) return -1;
    if (priority < PRIORITY_MIN) priority = PRIORITY_MIN;
    if (priority > PRIORITY_MAX) priority = PRIORITY_MAX;

    pthread_mutex_lock(&g_lock);
    if (g_stop || g_count >= MAX_TASKS) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    Task *t = &g_tasks[g_count];
    memset(t, 0, sizeof *t);
    t->id = g_count + 1;
    strncpy(t->command, command, MAX_CMD_LEN - 1);
    t->priority = priority;
    t->status = TASK_PENDING;
    t->submit_time = time(NULL);
    int id = t->id;
    g_count++;
    pthread_cond_signal(&g_cond);            /* wake one idle worker */
    pthread_mutex_unlock(&g_lock);
    return id;
}

int sched_list(Task *out, int max)
{
    pthread_mutex_lock(&g_lock);
    int n = g_count < max ? g_count : max;
    memcpy(out, g_tasks, (size_t)n * sizeof(Task));
    pthread_mutex_unlock(&g_lock);
    return n;
}

int sched_get(int id, Task *out)
{
    int rc = -1;
    pthread_mutex_lock(&g_lock);
    if (id >= 1 && id <= g_count) {
        *out = g_tasks[id - 1];
        rc = 0;
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}

int sched_kill(int id)
{
    int rc;
    pthread_mutex_lock(&g_lock);
    if (id < 1 || id > g_count) {
        rc = -1;
    } else {
        Task *t = &g_tasks[id - 1];
        if (t->status == TASK_PENDING) {          /* cancel before it ever runs */
            t->status = TASK_KILLED;
            t->end_time = time(NULL);
            rc = 0;
        } else if (t->status == TASK_RUNNING) {   /* worker will reap & mark KILLED */
            pm_signal_group(t->pid, SIGTERM);
            rc = 0;
        } else {
            rc = -2;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
