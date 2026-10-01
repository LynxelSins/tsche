#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#include "client/ipc_client.h"


static int ipc_connect(const char *path)
{
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof addr.sun_path) return -1;
    strcpy(addr.sun_path, path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int ipc_request(const Request *req, ResponseHeader *hdr, Task **tasks_out)
{
    if (tasks_out) *tasks_out = NULL;

    char path[256];
    protocol_socket_path(path, sizeof path);

    int fd = ipc_connect(path);
    if (fd < 0) return -1;

    if (protocol_write_full(fd, req, sizeof *req) < 0 ||
        protocol_read_full(fd, hdr, sizeof *hdr) < 0) {
        close(fd);
        return -1;
    }
    hdr->message[sizeof hdr->message - 1] = '\0';

    if (hdr->count > 0) {
        if (hdr->count > MAX_TASKS) { close(fd); return -1; }
        Task *buf = malloc(sizeof(Task) * (size_t)hdr->count);
        if (!buf || protocol_read_full(fd, buf, sizeof(Task) * (size_t)hdr->count) < 0) {
            free(buf);
            close(fd);
            return -1;
        }
        if (tasks_out) *tasks_out = buf; else free(buf);
    }
    close(fd);
    return 0;
}

static int daemon_reachable(void)
{
    char path[256];
    protocol_socket_path(path, sizeof path);
    int fd = ipc_connect(path);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

int ipc_ensure_daemon(void)
{
    if (daemon_reachable()) return 0;

    /* prefer the tasksched that sits next to this binary (bin/) */
    char exe[512], daemon_path[560] = "tasksched";
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash)
            snprintf(daemon_path, sizeof daemon_path, "%.*s/tasksched",
                     (int)(slash - exe), exe);
    }

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        setsid();                            /* survive Ctrl-C / closing the TUI */
        int fd = open("/dev/null", O_RDWR);
        if (fd >= 0) {
            dup2(fd, STDIN_FILENO);
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2) close(fd);
        }
        execl(daemon_path, "tasksched", (char *)NULL);
        execlp("tasksched", "tasksched", (char *)NULL);   /* fallback: PATH */
        _exit(127);
    }

    for (int i = 0; i < 20; i++) {           /* wait for the socket, max ~2 s */
        usleep(100 * 1000);
        if (daemon_reachable()) return 0;
    }
    return -1;
}