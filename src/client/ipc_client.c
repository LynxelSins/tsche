#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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
