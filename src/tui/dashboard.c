#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client/ipc_client.h"
#include "common/protocol.h"
#include "tui/dashboard.h"

enum {
    CP_PENDING = 1,
    CP_RUNNING,
    CP_DONE,
    CP_FAILED,
    CP_KILLED,
    CP_HEAD,
    CP_ACCENT
};

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

static const char *status_icon(TaskStatus s)
{
    switch (s) {
    case TASK_PENDING: return "o";
    case TASK_RUNNING: return "*";
    case TASK_DONE:    return "v";
    case TASK_FAILED:  return "x";
    case TASK_KILLED:  return "!";
    }
    return "?";
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
        snprintf(msg, len, "Daemon unreachable");
    else
        snprintf(msg, len, "Task #%d: %s", id, h.message);
}

static int submit_task(const char *command, int priority, char *msg, size_t len)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_SUBMIT;
    req.priority = priority;
    snprintf(req.command, sizeof req.command, "%s", command);

    if (ipc_request(&req, &h, NULL) < 0) {
        snprintf(msg, len, "Daemon unreachable");
        return -1;
    }
    if (h.code != RESP_OK) {
        snprintf(msg, len, "%s", h.message);
        return -1;
    }
    snprintf(msg, len, "Created task #%d (priority %d)", h.value, priority);
    return 0;
}

static void draw_header(int ok, const int cnt[5])
{
    attron(A_BOLD);
    mvprintw(0, 2, "TASK SCHEDULER");
    attroff(A_BOLD);

    if (!ok) {
        attron(COLOR_PAIR(CP_FAILED) | A_BOLD);
        mvprintw(1, 2, "! Daemon is not reachable - start ./bin/tasksched");
        attroff(COLOR_PAIR(CP_FAILED) | A_BOLD);
        return;
    }

    mvprintw(1, 2, "Total: %d", cnt[0] + cnt[1] + cnt[2] + cnt[3] + cnt[4]);
    attron(COLOR_PAIR(CP_PENDING) | A_BOLD);
    printw("   o Pending: %d", cnt[TASK_PENDING]);
    attroff(COLOR_PAIR(CP_PENDING) | A_BOLD);
    attron(COLOR_PAIR(CP_RUNNING) | A_BOLD);
    printw("   * Running: %d", cnt[TASK_RUNNING]);
    attroff(COLOR_PAIR(CP_RUNNING) | A_BOLD);
    attron(COLOR_PAIR(CP_DONE) | A_BOLD);
    printw("   v Done: %d", cnt[TASK_DONE]);
    attroff(COLOR_PAIR(CP_DONE) | A_BOLD);
    attron(COLOR_PAIR(CP_FAILED) | A_BOLD);
    printw("   x Failed: %d", cnt[TASK_FAILED]);
    attroff(COLOR_PAIR(CP_FAILED) | A_BOLD);
}

static void draw(const Task *tasks, int n, int sel, int ok, const char *msg)
{
    int cnt[5] = {0};
    for (int i = 0; i < n; i++) {
        if (tasks[i].status >= TASK_PENDING && tasks[i].status <= TASK_KILLED)
            cnt[tasks[i].status]++;
    }

    erase();
    draw_header(ok, cnt);

    int table_top = 3;
    int detail_height = 5;
    int footer_height = 2;
    int rows = LINES - table_top - detail_height - footer_height - 1;
    if (rows < 1) rows = 1;

    attron(COLOR_PAIR(CP_HEAD) | A_BOLD);
    mvprintw(table_top, 0, " %-5s %-11s %-4s %-8s %-7s %s", "ID", "STATUS", "PRI", "PID", "EXIT", "COMMAND");
    attroff(COLOR_PAIR(CP_HEAD) | A_BOLD);

    int top = sel >= rows ? sel - rows + 1 : 0;
    for (int r = 0; r < rows && top + r < n; r++) {
        const Task *t = &tasks[n - 1 - (top + r)];
        char pidbuf[16], exbuf[16];

        if (t->pid > 0) snprintf(pidbuf, sizeof pidbuf, "%d", (int)t->pid);
        else            snprintf(pidbuf, sizeof pidbuf, "-");

        if (t->status == TASK_PENDING || t->status == TASK_RUNNING)
            snprintf(exbuf, sizeof exbuf, "-");
        else if (t->term_signal)
            snprintf(exbuf, sizeof exbuf, "sig%d", t->term_signal);
        else
            snprintf(exbuf, sizeof exbuf, "%d", t->exit_code);

        char line[512];
        snprintf(line, sizeof line, " %-5d %s %-9s %-4d %-8s %-7s %s",
                 t->id, status_icon(t->status), task_status_str(t->status),
                 t->priority, pidbuf, exbuf, t->command);

        int y = table_top + 1 + r;
        int selected = (top + r == sel);
        if (selected) attron(A_REVERSE | A_BOLD);
        attron(COLOR_PAIR(pair_for(t->status)));
        mvprintw(y, 0, "%-*.*s", COLS, COLS, line);
        attroff(COLOR_PAIR(pair_for(t->status)));
        if (selected) attroff(A_REVERSE | A_BOLD);
    }

    int detail_y = table_top + rows + 1;
    if (detail_y < LINES - footer_height) {
        mvhline(detail_y, 0, ACS_HLINE, COLS);
        mvprintw(detail_y + 1, 2, "Selected task");

        if (n > 0) {
            const Task *t = &tasks[n - 1 - sel];
            attron(COLOR_PAIR(pair_for(t->status)) | A_BOLD);
            mvprintw(detail_y + 1, 18, "#%d  %s %s", t->id, status_icon(t->status), task_status_str(t->status));
            attroff(COLOR_PAIR(pair_for(t->status)) | A_BOLD);
            mvprintw(detail_y + 2, 2, "Command: %.100s", t->command);
            mvprintw(detail_y + 3, 2, "Priority: %d   PID: %d", t->priority, (int)t->pid);
        } else {
            mvprintw(detail_y + 2, 2, "No tasks yet. Press N to create one.");
        }
    }

    mvhline(LINES - 2, 0, ACS_HLINE, COLS);
    if (msg && msg[0]) mvprintw(LINES - 2, 2, " %.*s", COLS - 4, msg);
    attron(A_BOLD);
    mvprintw(LINES - 1, 1, "Up/Down Select  Enter Details  N New  K Kill  L Logs  R Refresh  ? Help  Q Quit");
    attroff(A_BOLD);
    refresh();
}

static void popup_message(const char *title, const char *body)
{
    int h = 8, w = COLS - 8;
    if (w > 70) w = 70;
    if (w < 30) w = 30;
    int y = (LINES - h) / 2, x = (COLS - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "%.*s", w - 4, title);
    wattroff(win, A_BOLD);
    mvwprintw(win, 3, 2, "%.*s", w - 4, body);
    mvwprintw(win, h - 2, 2, "Press any key to close");
    wrefresh(win);
    wgetch(win);
    delwin(win);
}

static void show_help(void)
{
    popup_message("Keyboard shortcuts",
                  "Up/Down Select | Enter Details | N New Task | K Kill | L Logs | R Refresh | Q Quit");
}

static void show_details(const Task *t)
{
    char body[512];
    char exitbuf[32];
    if (t->status == TASK_PENDING || t->status == TASK_RUNNING)
        snprintf(exitbuf, sizeof exitbuf, "-");
    else if (t->term_signal)
        snprintf(exitbuf, sizeof exitbuf, "signal %d", t->term_signal);
    else
        snprintf(exitbuf, sizeof exitbuf, "%d", t->exit_code);

    snprintf(body, sizeof body,
             "ID #%d | Status: %s | Priority: %d | PID: %d | Exit: %s | Command: %s",
             t->id, task_status_str(t->status), t->priority, (int)t->pid, exitbuf, t->command);
    popup_message("Task details", body);
}

static int confirm_kill(const Task *t)
{
    int h = 8, w = 56;
    if (w > COLS - 4) w = COLS - 4;
    int y = (LINES - h) / 2, x = (COLS - w) / 2;
    WINDOW *win = newwin(h, w, y, x);
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 1, 2, "Kill task #%d?", t->id);
    wattroff(win, A_BOLD);
    mvwprintw(win, 3, 2, "%.45s", t->command);
    mvwprintw(win, 5, 2, "Y = Yes    N = No");
    wrefresh(win);
    int ch;
    do ch = wgetch(win); while (ch != 'y' && ch != 'Y' && ch != 'n' && ch != 'N' && ch != 27);
    delwin(win);
    return ch == 'y' || ch == 'Y';
}

static void new_task(char *msg, size_t msg_len)
{
    char command[MAX_CMD_LEN] = "";
    char priority_text[16] = "5";

    int h = 10, w = COLS - 8;
    if (w > 76) w = 76;
    if (w < 40) w = 40;
    int y = (LINES - h) / 2, x = (COLS - w) / 2;
    WINDOW *win = newwin(h, w, y, x);
    box(win, 0, 0);
    keypad(win, TRUE);

    mvwprintw(win, 1, 2, "New Task");
    mvwprintw(win, 3, 2, "Command:");
    mvwprintw(win, 5, 2, "Priority (0-9):");
    mvwprintw(win, 7, 2, "Enter = Submit   Esc = Cancel");

    echo();
    curs_set(1);
    mvwgetnstr(win, 3, 12, command, MAX_CMD_LEN - 1);
    if (command[0] == '\0') {
        noecho();
        curs_set(0);
        delwin(win);
        snprintf(msg, msg_len, "Cancelled: command is empty");
        return;
    }
    mvwgetnstr(win, 5, 18, priority_text, 2);
    noecho();
    curs_set(0);
    delwin(win);

    char *end;
    long priority = strtol(priority_text, &end, 10);
    if (*priority_text == '\0' || *end != '\0' || priority < PRIORITY_MIN || priority > PRIORITY_MAX) {
        snprintf(msg, msg_len, "Priority must be %d-%d", PRIORITY_MIN, PRIORITY_MAX);
        return;
    }
    submit_task(command, (int)priority, msg, msg_len);
}

static void show_logs(const Task *t)
{
    char path[256];
    protocol_log_path(t->id, path, sizeof path);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        popup_message("Task logs", "No log is available for this task yet.");
        return;
    }

    char lines[12][COLS > 4 ? COLS : 80];
    int count = 0;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        snprintf(lines[count % 12], sizeof lines[0], "%.*s", COLS - 6, buf);
        count++;
    }
    fclose(fp);

    int h = LINES - 4;
    int w = COLS - 4;
    WINDOW *win = newwin(h, w, 2, 2);
    box(win, 0, 0);
    mvwprintw(win, 1, 2, "Task #%d logs (last %d lines)", t->id, count < 12 ? count : 12);
    int start = count > 12 ? count - 12 : 0;
    for (int i = start; i < count && i < start + h - 4; i++)
        mvwprintw(win, 3 + i - start, 2, "%.*s", w - 4, lines[i % 12]);
    mvwprintw(win, h - 2, 2, "Press any key to go back");
    wrefresh(win);
    wgetch(win);
    delwin(win);
}

int dashboard_run(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(500);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_PENDING, COLOR_YELLOW, -1);
        init_pair(CP_RUNNING, COLOR_CYAN, -1);
        init_pair(CP_DONE, COLOR_GREEN, -1);
        init_pair(CP_FAILED, COLOR_RED, -1);
        init_pair(CP_KILLED, COLOR_MAGENTA, -1);
        init_pair(CP_HEAD, COLOR_BLACK, COLOR_WHITE);
        init_pair(CP_ACCENT, COLOR_WHITE, COLOR_BLUE);
    }

    Task *tasks = NULL;
    int n = 0, sel = 0, running = 1;
    char msg[160] = "Ready";

    while (running) {
        int ok = (fetch(&tasks, &n) == 0);
        if (sel >= n) sel = n > 0 ? n - 1 : 0;
        draw(tasks, n, sel, ok, msg);

        int ch = getch();
        switch (ch) {
        case 'q': case 'Q':
            running = 0;
            break;
        case KEY_UP:
            if (sel > 0) sel--;
            break;
        case KEY_DOWN:
            if (sel < n - 1) sel++;
            break;
        case 'r': case 'R':
            snprintf(msg, sizeof msg, "Refreshed");
            break;
        case '?':
            show_help();
            break;
        case 10: case KEY_ENTER:
            if (ok && n > 0) show_details(&tasks[n - 1 - sel]);
            break;
        case 'k': case 'K':
            if (ok && n > 0 && confirm_kill(&tasks[n - 1 - sel]))
                kill_task(tasks[n - 1 - sel].id, msg, sizeof msg);
            break;
        case 'n': case 'N':
            new_task(msg, sizeof msg);
            break;
        case 'l': case 'L':
            if (ok && n > 0) show_logs(&tasks[n - 1 - sel]);
            break;
        case KEY_RESIZE:
            clear();
            break;
        default:
            break;
        }
        free(tasks);
        tasks = NULL;
    }

    endwin();
    return 0;
}
