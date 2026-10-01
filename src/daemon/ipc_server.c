#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/protocol.h"
#include "daemon/ipc_server.h"
#include "daemon/scheduler.h"

/* Is another daemon already listening on this path? */
static int daemon_alive(const struct sockaddr_un *addr)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    int ok = connect(fd, (const struct sockaddr *)addr, sizeof *addr) == 0;
    close(fd);
    return ok;
}

int ipc_server_start(const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof addr.sun_path) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(addr.sun_path, path);

    if (daemon_alive(&addr)) {
        errno = EADDRINUSE;
        return -1;
    }
    unlink(path);                            /* remove stale socket file */

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    mode_t old = umask(0077);                /* socket file: owner only */
    int rc = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    umask(old);
    if (rc < 0 || listen(fd, 16) < 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

static void set_msg(ResponseHeader *h, int code, const char *msg)
{
    h->code = code;
    snprintf(h->message, sizeof h->message, "%s", msg);
}

static void handle_client(int cfd, volatile sig_atomic_t *stop)
{
    /* Only the same user may talk to us: tasks run arbitrary commands! */
    struct ucred cred;
    socklen_t len = sizeof cred;
    if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0 ||
        cred.uid != getuid())
        return;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };   /* slow client guard */
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    Request req;
    if (protocol_read_full(cfd, &req, sizeof req) < 0)
        return;
    req.command[MAX_CMD_LEN - 1] = '\0';

    ResponseHeader h;
    memset(&h, 0, sizeof h);
    Task *tasks = NULL;

    switch (req.cmd) {
    case CMD_SUBMIT: {
        int id = sched_submit(req.command, req.priority);
        if (id < 0) {
            set_msg(&h, RESP_ERR, "submit failed (empty command, queue full or shutting down)");
        } else {
            h.value = id;
            set_msg(&h, RESP_OK, "task submitted");
        }
        break;
    }
    case CMD_LIST:
        tasks = malloc(sizeof(Task) * MAX_TASKS);
        if (!tasks) { set_msg(&h, RESP_ERR, "out of memory"); break; }
        h.count = sched_list(tasks, MAX_TASKS);
        set_msg(&h, RESP_OK, "ok");
        break;
    case CMD_STATUS:
        tasks = malloc(sizeof(Task));
        if (!tasks) { set_msg(&h, RESP_ERR, "out of memory"); break; }
        if (sched_get(req.task_id, tasks) == 0) {
            h.count = 1;
            set_msg(&h, RESP_OK, "ok");
        } else {
            set_msg(&h, RESP_ERR, "no such task");
        }
        break;
    case CMD_KILL: {
        int rc = sched_kill(req.task_id);
        if (rc == 0)       set_msg(&h, RESP_OK, "terminate signal sent / task cancelled");
        else if (rc == -1) set_msg(&h, RESP_ERR, "no such task");
        else               set_msg(&h, RESP_ERR, "task already finished");
        break;
    }
    case CMD_SHUTDOWN:
        *stop = 1;
        set_msg(&h, RESP_OK, "daemon shutting down");
        break;
    default:
        set_msg(&h, RESP_ERR, "unknown command");
        break;
    }

    if (protocol_write_full(cfd, &h, sizeof h) == 0 && h.count > 0 && tasks)
        protocol_write_full(cfd, tasks, sizeof(Task) * (size_t)h.count);
    free(tasks);
}

void ipc_server_run(int listen_fd, volatile sig_atomic_t *stop)
{
    while (!*stop) {
        struct pollfd p = { .fd = listen_fd, .events = POLLIN };
        int r = poll(&p, 1, 300);            /* wake up regularly to check *stop */
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }
        if (r == 0) continue;

        int cfd = accept4(listen_fd, NULL, NULL, SOCK_CLOEXEC);
        if (cfd < 0) continue;
        handle_client(cfd, stop);
        close(cfd);
    }
}
