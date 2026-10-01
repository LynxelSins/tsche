#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common/protocol.h"

int protocol_read_full(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;          /* peer closed early */
        got += (size_t)r;
    }
    return 0;
}

int protocol_write_full(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t sent = 0;
    while (sent < n) {
        /* MSG_NOSIGNAL: a vanished peer gives EPIPE instead of SIGPIPE */
        ssize_t w = send(fd, p + sent, n - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)w;
    }
    return 0;
}

void protocol_socket_path(char *buf, size_t n)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && *rt)
        snprintf(buf, n, "%s/tasksched.sock", rt);
    else
        snprintf(buf, n, "/tmp/tasksched-%u.sock", (unsigned)getuid());
}

void protocol_log_dir(char *buf, size_t n)
{
    snprintf(buf, n, "/tmp/tasksched-%u-logs", (unsigned)getuid());
}

void protocol_log_path(int task_id, char *buf, size_t n)
{
    char dir[200];
    protocol_log_dir(dir, sizeof dir);
    snprintf(buf, n, "%s/task-%d.log", dir, task_id);
}
