#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/ipc_client.h"
#include "tui/dashboard.h"

enum { CP_PENDING = 1, CP_RUNNING, CP_DONE, CP_FAILED, CP_KILLED, CP_HEAD };

static int pair_for(TaskStatus s)
{
    switch (s) {
    case TASK_PENDING: return CP_PENDING;
    case TASK_RUNNING: return CP_RUNNING;
    case TASK_DONE:    return CP_DONE;
    case TASK_FAILED:  return CP_FAILED;
    case TASK_KILLED:  return CP_KILLED;
    }
    return 0;
}

static int fetch(Task **tasks, int *n)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_LIST;
    *tasks = NULL;
    *n = 0;
    if (ipc_request(&req, &h, tasks) < 0 || h.code != RESP_OK) {
        free(*tasks);
        *tasks = NULL;
        return -1;
    }
    *n = h.count;
    return 0;
}

static void kill_task(int id, char *msg, size_t len)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_KILL;
    req.task_id = id;
    if (ipc_request(&req, &h, NULL) < 0)
        snprintf(msg, len, "daemon unreachable");
    else
        snprintf(msg, len, "task %d: %s", id, h.message);
}

static void draw(const Task *tasks, int n, int sel, int ok, const char *msg)
{
    int cnt[5] = {0};
    for (int i = 0; i < n; i++) cnt[tasks[i].status]++;

    erase();
    attron(A_BOLD);
    if (ok)
        mvprintw(0, 1, "tasksched dashboard   pending:%d running:%d done:%d failed:%d killed:%d",
                 cnt[TASK_PENDING], cnt[TASK_RUNNING], cnt[TASK_DONE],
                 cnt[TASK_FAILED], cnt[TASK_KILLED]);
    else
        mvprintw(0, 1, "tasksched dashboard   [daemon not reachable - start ./bin/tasksched]");
    attroff(A_BOLD);

    attron(COLOR_PAIR(CP_HEAD));
    mvprintw(2, 0, "%-*.*s", COLS, COLS, " ID   PRI STATUS   PID     EXIT   COMMAND");
    attroff(COLOR_PAIR(CP_HEAD));

    int rows = LINES - 5;
    if (rows < 1) rows = 1;
    int top = sel >= rows ? sel - rows + 1 : 0;

    for (int r = 0; r < rows && top + r < n; r++) {
        /* newest task first */
        const Task *t = &tasks[n - 1 - (top + r)];
        char pidbuf[16], exbuf[16], line[512];

        if (t->pid > 0) snprintf(pidbuf, sizeof pidbuf, "%d", (int)t->pid);
        else            snprintf(pidbuf, sizeof pidbuf, "-");
        if (t->status == TASK_PENDING || t->status == TASK_RUNNING)
            snprintf(exbuf, sizeof exbuf, "-");
        else if (t->term_signal)
            snprintf(exbuf, sizeof exbuf, "sig%d", t->term_signal);
        else
            snprintf(exbuf, sizeof exbuf, "%d", t->exit_code);

        snprintf(line, sizeof line, " %-4d %-3d %-8s %-7s %-6s %s",
                 t->id, t->priority, task_status_str(t->status),
                 pidbuf, exbuf, t->command);

        int y = 3 + r;
        int selected = (top + r == sel);
        if (selected) attron(A_REVERSE);
        attron(COLOR_PAIR(pair_for(t->status)));
        mvprintw(y, 0, "%-*.*s", COLS, COLS, line);
        attroff(COLOR_PAIR(pair_for(t->status)));
        if (selected) attroff(A_REVERSE);
    }

    mvprintw(LINES - 2, 1, "%s", msg);
    mvprintw(LINES - 1, 1, "Up/Down: select   x: kill selected   q: quit");
    refresh();
}

int dashboard_run(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(500);                            /* getch() returns every 500 ms -> refresh */

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_PENDING, COLOR_YELLOW, -1);
        init_pair(CP_RUNNING, COLOR_CYAN, -1);
        init_pair(CP_DONE,    COLOR_GREEN, -1);
        init_pair(CP_FAILED,  COLOR_RED, -1);
        init_pair(CP_KILLED,  COLOR_MAGENTA, -1);
        init_pair(CP_HEAD,    COLOR_BLACK, COLOR_WHITE);
    }

    Task *tasks = NULL;
    int n = 0, sel = 0, running = 1;
    char msg[160] = "";

    while (running) {
        int ok = (fetch(&tasks, &n) == 0);
        if (sel >= n) sel = n > 0 ? n - 1 : 0;

        draw(tasks, n, sel, ok, msg);

        switch (getch()) {
        case 'q': case 'Q': running = 0; break;
        case KEY_UP:   if (sel > 0) sel--; break;
        case KEY_DOWN: if (sel < n - 1) sel++; break;
        case 'x': case 'X':
            if (ok && n > 0) kill_task(tasks[n - 1 - sel].id, msg, sizeof msg);
            break;
        default: break;                      /* ERR (timeout) or KEY_RESIZE */
        }
        free(tasks);
        tasks = NULL;
    }

    endwin();
    return 0;
}
