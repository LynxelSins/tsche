#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/protocol.h"
#include "daemon/ipc_server.h"
#include "daemon/process_manager.h"
#include "daemon/scheduler.h"

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;                              /* only set a flag in the handler */
}

int main(int argc, char **argv)
{
    int workers = 4;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            workers = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: %s [-w workers]\n", argv[0]);
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;               /* no SA_RESTART: poll() returns EINTR */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    char path[256];
    protocol_socket_path(path, sizeof path);

    if (pm_init() < 0) return 1;
    int lfd = ipc_server_start(path);
    if (lfd < 0) {
        perror("tasksched: cannot start server (already running?)");
        return 1;
    }
    if (sched_init(workers) < 0) {
        fprintf(stderr, "tasksched: cannot start workers\n");
        close(lfd);
        unlink(path);
        return 1;
    }

    printf("tasksched: listening on %s (%d workers)\n", path, workers);
    fflush(stdout);

    ipc_server_run(lfd, &g_stop);

    printf("tasksched: shutting down...\n");
    close(lfd);
    unlink(path);
    sched_shutdown();
    printf("tasksched: bye\n");
    return 0;
}
