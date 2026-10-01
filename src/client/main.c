#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>      /* เพิ่ม: time_t, struct tm, localtime_r, strftime */
#include <unistd.h>

#include "client/ipc_client.h"
#include "common/protocol.h"
#include "tui/dashboard.h"

/* ---- forward declarations (จำเป็นเพราะ main อยู่ท้ายไฟล์) ---- */
static void usage(void);
static int  do_request(const Request *req, ResponseHeader *h, Task **tasks);
static int  parse_id(const char *s);
static int  cmd_submit(int argc, char **argv);
static int  cmd_list(void);
static int  cmd_status(int id);
static int  cmd_simple(int cmd, int id);
static int  cmd_logs(int id);

/* ---- helpers ---- */

static void usage(void)
{
    fputs("usage:\n"
        "  sche submit [-p 0-9] <command...>  submit a task (higher priority runs first)\n"
        "  sche list                          list all tasks\n"
        "  sche status <id>                   show task details\n"
        "  sche kill <id>                     cancel a pending task / SIGTERM a running one\n"
        "  sche logs <id>                     print task output\n"
        "  sche top                           live ncurses dashboard\n"
        "  sche shutdown                      stop the daemon\n",
        stderr);
}

static int do_request(const Request *req, ResponseHeader *h, Task **tasks)
{
    if (ipc_request(req, h, tasks) < 0)
    {
        fputs("sche: cannot reach the daemon (is 'tasksched' running?)\n", stderr);
        return -1;
    }
    if (h->code != RESP_OK)
    {
        fprintf(stderr, "sche: %s\n", h->message);
        return -1;
    }
    return 0;
}

static int parse_id(const char *s)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 1 || v > MAX_TASKS)
    {
        fprintf(stderr, "sche: invalid task id '%s'\n", s);
        return -1;
    }
    return (int)v;
}

static void fmt_time(time_t t, char *buf, size_t n)
{
    if (t == 0) { snprintf(buf, n, "-"); return; }
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%H:%M:%S", &tm);
}

static void fmt_exit(const Task *t, char *buf, size_t n)
{
    if (!task_is_finished(t->status))
        snprintf(buf, n, "-");
    else if (t->term_signal)
        snprintf(buf, n, "sig%d", t->term_signal);
    else
        snprintf(buf, n, "%d", t->exit_code);
}

/* ---- commands ---- */

static int cmd_submit(int argc, char **argv)
{
    int prio = PRIORITY_DEFAULT, i = 2;

    if (i < argc && strcmp(argv[i], "-p") == 0)
    {
        if (i + 1 >= argc) { usage(); return 2; }
        char *end;
        long v = strtol(argv[i + 1], &end, 10);
        if (*end != '\0' || v < PRIORITY_MIN || v > PRIORITY_MAX)
        {
            fprintf(stderr, "sche: priority must be %d-%d\n",
                    PRIORITY_MIN, PRIORITY_MAX);
            return 2;
        }
        prio = (int)v;
        i += 2;
    }
    if (i >= argc) { usage(); return 2; }

    Request req;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_SUBMIT;
    req.priority = prio;

    size_t len = 0;
    for (; i < argc; i++)
    {
        size_t l = strlen(argv[i]);
        if (len + l + 2 >= MAX_CMD_LEN)
        {
            fprintf(stderr, "sche: command too long (max %d chars)\n",
                    MAX_CMD_LEN - 1);
            return 2;
        }
        if (len) req.command[len++] = ' ';
        memcpy(req.command + len, argv[i], l);
        len += l;
    }
    req.command[len] = '\0';    /* ปิด string ให้ปลอดภัย */

    ResponseHeader h;
    if (do_request(&req, &h, NULL) < 0)
        return 1;
    printf("submitted task %d (priority %d)\n", h.value, prio);
    return 0;
}

static int cmd_list(void)
{
    Request req;
    ResponseHeader h;
    Task *tasks = NULL;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_LIST;
    if (do_request(&req, &h, &tasks) < 0)
        return 1;

    printf("%-4s %-3s %-8s %-7s %-6s %s\n",
           "ID", "PRI", "STATUS", "PID", "EXIT", "COMMAND");
    for (int i = 0; i < h.count; i++)
    {
        const Task *t = &tasks[i];
        char pid[16], ex[16];
        if (t->pid > 0) snprintf(pid, sizeof pid, "%d", (int)t->pid);
        else            snprintf(pid, sizeof pid, "-");
        fmt_exit(t, ex, sizeof ex);
        printf("%-4d %-3d %-8s %-7s %-6s %s\n",
               t->id, t->priority, task_status_str(t->status),
               pid, ex, t->command);
    }
    if (h.count == 0) puts("(no tasks)");
    free(tasks);
    return 0;
}

static int cmd_status(int id)
{
    Request req;
    ResponseHeader h;
    Task *tasks = NULL;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_STATUS;
    req.task_id = id;
    if (do_request(&req, &h, &tasks) < 0 || h.count < 1)
        return 1;

    const Task *t = &tasks[0];
    char sub[16], sta[16], end[16], ex[16];
    fmt_time(t->submit_time, sub, sizeof sub);
    fmt_time(t->start_time,  sta, sizeof sta);
    fmt_time(t->end_time,    end, sizeof end);
    fmt_exit(t, ex, sizeof ex);

    printf("Task %d\n"
           "  command  : %s\n"
           "  priority : %d\n"
           "  status   : %s\n"
           "  pid      : %d\n"
           "  exit     : %s\n"
           "  submitted: %s\n"
           "  started  : %s\n"
           "  finished : %s\n",
           t->id, t->command, t->priority, task_status_str(t->status),
           (int)t->pid, ex, sub, sta, end);
    free(tasks);
    return 0;
}

static int cmd_simple(int cmd, int id)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = cmd;
    req.task_id = id;
    if (do_request(&req, &h, NULL) < 0)
        return 1;
    puts(h.message);
    return 0;
}

static int cmd_logs(int id)
{
    char path[256];
    protocol_log_path(id, path, sizeof path);
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "sche: no log for task %d yet (%s)\n",
                id, strerror(errno));
        return 1;
    }
    char buf[4096];
    ssize_t r;
    while ((r = read(fd, buf, sizeof buf)) > 0)
        if (write(STDOUT_FILENO, buf, (size_t)r) < 0)
            break;
    close(fd);
    return 0;
}

/* ---- main (อันเดียว) ---- */

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        /* ตามที่โค้ดอันบนตั้งใจ: ไม่มี argument -> เปิด TUI */
        if (ipc_ensure_daemon() < 0)
            fputs("sche: could not start tasksched\n", stderr);
        return dashboard_run();
    }
    const char *c = argv[1];

    if (!strcmp(c, "submit")) return cmd_submit(argc, argv);
    if (!strcmp(c, "list"))   return cmd_list();
    if (!strcmp(c, "top"))
    {
        if (ipc_ensure_daemon() < 0)
            fputs("sche: could not start tasksched\n", stderr);
        return dashboard_run();
    }
    if (!strcmp(c, "shutdown")) return cmd_simple(CMD_SHUTDOWN, 0);

    if (!strcmp(c, "status") || !strcmp(c, "kill") || !strcmp(c, "logs"))
    {
        if (argc != 3) { usage(); return 2; }
        int id = parse_id(argv[2]);
        if (id < 0) return 2;
        if (!strcmp(c, "status")) return cmd_status(id);
        if (!strcmp(c, "kill"))   return cmd_simple(CMD_KILL, id);
        return cmd_logs(id);
    }

    usage();
    return 2;
}