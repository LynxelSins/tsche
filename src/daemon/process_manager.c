#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/protocol.h"
#include "daemon/process_manager.h"

int pm_init(void)
{
    char dir[200];
    protocol_log_dir(dir, sizeof dir);
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
        perror("pm_init: mkdir log dir");
        return -1;
    }
    return 0;
}

pid_t pm_spawn(const Task *t)
{
    /* Prepare everything that is not async-signal-safe BEFORE fork():
     * the daemon is multi-threaded, so the child may only call
     * async-signal-safe functions until exec. */
    char logpath[256];
    protocol_log_path(t->id, logpath, sizeof logpath);
    char *argv[] = { "sh", "-c", (char *)t->command, NULL };

    pid_t pid = fork();
    if (pid < 0)
        return -1;

    if (pid == 0) {
        setpgid(0, 0);                       /* own process group -> kill(-pgid) */

        int in = open("/dev/null", O_RDONLY);
        if (in >= 0) { dup2(in, STDIN_FILENO); if (in > 2) close(in); }

        int out = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out >= 0) {
            dup2(out, STDOUT_FILENO);
            dup2(out, STDERR_FILENO);
            if (out > 2) close(out);
        }

        execv("/bin/sh", argv);
        _exit(127);                          /* exec failed */
    }

    setpgid(pid, pid);                       /* also from parent: avoids a race */
    return pid;
}

int pm_wait(pid_t pid, int *exit_code, int *term_signal)
{
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno == EINTR) continue;
        return -1;
    }
    *exit_code = 0;
    *term_signal = 0;
    if (WIFEXITED(st))
        *exit_code = WEXITSTATUS(st);
    else if (WIFSIGNALED(st))
        *term_signal = WTERMSIG(st);
    return 0;
}

int pm_signal_group(pid_t pid, int sig)
{
    if (pid <= 0) return -1;
    return kill(-pid, sig);
}
