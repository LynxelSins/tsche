#include <fcntl.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client/ipc_client.h"
#include "common/protocol.h"
#include "tui/dashboard.h"

#define INPUT_MAX 300            /* > "submit -p 9 " + MAX_CMD_LEN */
#define HIST_MAX  50
#define LOG_TAIL  16384          /* only the last 16 KB of a log is shown */

enum { CP_PENDING = 1, CP_RUNNING, CP_DONE, CP_FAILED, CP_KILLED, CP_HEAD };
typedef enum { FOCUS_PROMPT, FOCUS_LIST } Focus;

typedef struct {
    char  input[INPUT_MAX];
    int   len;
    char  hist[HIST_MAX][INPUT_MAX];
    int   nhist, hpos;           /* hpos == nhist -> editing a fresh line */
    Focus focus;
    int   sel;                   /* selected row (0 = newest task) */
    int   log_id;                /* 0 = log pane closed */
    char  msg[160];
    int   running;
} UI;

/* ---------- small helpers ---------- */

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

static void set_msg(UI *ui, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void set_msg(UI *ui, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->msg, sizeof ui->msg, fmt, ap);
    va_end(ap);
}

static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* copy the next whitespace-delimited word into w; return pointer to the rest */
static const char *next_word(const char *s, char *w, size_t n)
{
    s = skip_ws(s);
    size_t i = 0;
    while (*s && *s != ' ' && *s != '\t') {
        if (i + 1 < n) w[i++] = *s;
        s++;
    }
    w[i] = '\0';
    return skip_ws(s);
}

static int parse_id(const char *s)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 1 || v > MAX_TASKS) return -1;
    return (int)v;
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

/* ---------- prompt commands (same vocabulary as the CLI) ---------- */

static void cmd_submit(UI *ui, const char *rest)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = CMD_SUBMIT;
    req.priority = PRIORITY_DEFAULT;

    char w[32];
    const char *p = next_word(rest, w, sizeof w);
    if (strcmp(w, "-p") == 0) {
        char pv[32], *end;
        p = next_word(p, pv, sizeof pv);
        long v = strtol(pv, &end, 10);
        if (*pv == '\0' || *end != '\0' || v < PRIORITY_MIN || v > PRIORITY_MAX) {
            set_msg(ui, "priority must be %d-%d", PRIORITY_MIN, PRIORITY_MAX);
            return;
        }
        req.priority = (int)v;
        rest = p;
    }
    if (*rest == '\0') { set_msg(ui, "usage: submit [-p 0-9] <command>"); return; }
    if (strlen(rest) >= MAX_CMD_LEN) {
        set_msg(ui, "command too long (max %d chars)", MAX_CMD_LEN - 1);
        return;
    }
    snprintf(req.command, sizeof req.command, "%s", rest);

    if (ipc_request(&req, &h, NULL) < 0)      set_msg(ui, "daemon unreachable");
    else if (h.code != RESP_OK)               set_msg(ui, "error: %s", h.message);
    else set_msg(ui, "submitted task %d (priority %d)", h.value, req.priority);
}

static void cmd_simple(UI *ui, int cmd, int id)
{
    Request req;
    ResponseHeader h;
    memset(&req, 0, sizeof req);
    req.cmd = cmd;
    req.task_id = id;
    if (ipc_request(&req, &h, NULL) < 0) set_msg(ui, "daemon unreachable");
    else                                 set_msg(ui, "%s", h.message);
}

static void run_command(UI *ui, const char *line)
{
    char cmd[32], arg[32];
    const char *rest = next_word(line, cmd, sizeof cmd);
    if (cmd[0] == '\0') return;

    if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) { ui->running = 0; return; }
    if (!strcmp(cmd, "help")) {
        set_msg(ui, "submit [-p N] <cmd> | kill <id> | logs <id> | close | shutdown | quit   (Tab: focus list)");
        return;
    }
    if (!strcmp(cmd, "close"))    { ui->log_id = 0; ui->msg[0] = '\0'; return; }
    if (!strcmp(cmd, "shutdown")) { cmd_simple(ui, CMD_SHUTDOWN, 0); return; }
    if (!strcmp(cmd, "submit"))   { cmd_submit(ui, rest); return; }

    if (!strcmp(cmd, "kill") || !strcmp(cmd, "logs")) {
        next_word(rest, arg, sizeof arg);
        int id = parse_id(arg);
        if (id < 0) { set_msg(ui, "usage: %s <id>", cmd); return; }
        if (cmd[0] == 'k') cmd_simple(ui, CMD_KILL, id);
        else { ui->log_id = id; set_msg(ui, "showing logs of task %d (type 'close' or Esc to hide)", id); }
        return;
    }
    set_msg(ui, "unknown command '%s' (try 'help')", cmd);
}

/* ---------- drawing ---------- */

/* read the last bytes of a task's log file; returns length or -1 */
static int read_tail(int id, char *buf, size_t cap)
{
    char path[256];
    protocol_log_path(id, path, sizeof path);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    off_t sz = lseek(fd, 0, SEEK_END);
    off_t off = sz > (off_t)(cap - 1) ? sz - (off_t)(cap - 1) : 0;
    lseek(fd, off, SEEK_SET);
    ssize_t r = read(fd, buf, cap - 1);
    close(fd);
    if (r < 0) return -1;
    buf[r] = '\0';
    return (int)r;
}

static void draw_logs(int id, int y0, int height)
{
    static char buf[LOG_TAIL];
    attron(COLOR_PAIR(CP_HEAD));
    mvprintw(y0, 0, "%-*.*s", COLS, COLS, "");
    mvprintw(y0, 1, " log of task %d ", id);
    attroff(COLOR_PAIR(CP_HEAD));

    int n = read_tail(id, buf, sizeof buf);
    if (n < 0) { mvprintw(y0 + 1, 1, "(no log yet)"); return; }

    /* find the start of the last `height` lines */
    int end = n;
    if (end > 0 && buf[end - 1] == '\n') end--;
    int start = end, lines = 0;
    while (start > 0 && lines < height) {
        if (buf[start - 1] == '\n') lines++;
        if (lines < height) start--; else break;
    }

    int y = y0 + 1;
    for (int i = start; i <= end && y < y0 + 1 + height; ) {
        char tmp[512];
        int k = 0;
        while (i < end && buf[i] != '\n') {
            unsigned char c = (unsigned char)buf[i++];
            if (k < COLS - 2 && k < (int)sizeof tmp - 1)
                tmp[k++] = (c >= 32 && c < 127) ? (char)c : (c == '\t' ? ' ' : '?');
        }
        tmp[k] = '\0';
        mvprintw(y++, 1, "%s", tmp);
        i++;                                  /* skip the '\n' */
    }
}

static void draw(const UI *ui, const Task *tasks, int n, int ok)
{
    int cnt[5] = {0};
    for (int i = 0; i < n; i++) cnt[tasks[i].status]++;

    erase();
    attron(A_BOLD);
    if (ok)
        mvprintw(0, 1, "tasksched   pending:%d running:%d done:%d failed:%d killed:%d",
                 cnt[TASK_PENDING], cnt[TASK_RUNNING], cnt[TASK_DONE],
                 cnt[TASK_FAILED], cnt[TASK_KILLED]);
    else
        mvprintw(0, 1, "tasksched   [daemon not reachable]");
    attroff(A_BOLD);

    attron(COLOR_PAIR(CP_HEAD));
    mvprintw(2, 0, "%-*.*s", COLS, COLS, " ID   PRI STATUS   PID     EXIT   COMMAND");
    attroff(COLOR_PAIR(CP_HEAD));

    /* rows 3 .. LINES-4 are shared by the task list and the (optional) log pane */
    int avail = LINES - 6;
    if (avail < 2) avail = 2;
    int list_h = ui->log_id ? (avail + 1) / 2 : avail;
    if (list_h < 1) list_h = 1;
    int top = ui->sel >= list_h ? ui->sel - list_h + 1 : 0;

    for (int r = 0; r < list_h && top + r < n; r++) {
        const Task *t = &tasks[n - 1 - (top + r)];     /* newest first */
        char pidbuf[16], exbuf[16], line[512];

        if (t->pid > 0) snprintf(pidbuf, sizeof pidbuf, "%d", (int)t->pid);
        else            snprintf(pidbuf, sizeof pidbuf, "-");
        if (!task_is_finished(t->status))  snprintf(exbuf, sizeof exbuf, "-");
        else if (t->term_signal)           snprintf(exbuf, sizeof exbuf, "sig%d", t->term_signal);
        else                               snprintf(exbuf, sizeof exbuf, "%d", t->exit_code);

        snprintf(line, sizeof line, " %-4d %-3d %-8s %-7s %-6s %s",
                 t->id, t->priority, task_status_str(t->status), pidbuf, exbuf, t->command);

        int selected = (ui->focus == FOCUS_LIST && top + r == ui->sel);
        if (selected) attron(A_REVERSE);
        attron(COLOR_PAIR(pair_for(t->status)));
        mvprintw(3 + r, 0, "%-*.*s", COLS, COLS, line);
        attroff(COLOR_PAIR(pair_for(t->status)));
        if (selected) attroff(A_REVERSE);
    }

    if (ui->log_id)
        draw_logs(ui->log_id, 3 + list_h, avail - list_h - 1 > 0 ? avail - list_h - 1 : 1);

    mvprintw(LINES - 3, 1, "%.*s", COLS - 2, ui->msg);

    /* prompt line; scroll horizontally if the input is longer than the screen */
    int vis = COLS - 3 > 1 ? COLS - 3 : 1;
    int off = ui->len > vis ? ui->len - vis : 0;
    if (ui->focus == FOCUS_PROMPT) attron(A_BOLD);
    mvprintw(LINES - 2, 0, "> ");
    if (ui->focus == FOCUS_PROMPT) attroff(A_BOLD);
    mvprintw(LINES - 2, 2, "%.*s", vis, ui->input + off);

    attron(A_DIM);
    if (ui->focus == FOCUS_PROMPT)
        mvprintw(LINES - 1, 1, "Enter: run   Up/Down: history   Tab: select tasks   Esc: close log   Ctrl-D: quit");
    else
        mvprintw(LINES - 1, 1, "Up/Down: select   Enter: show log   x: kill   Tab: prompt   Esc: close log   q: quit");
    attroff(A_DIM);

    if (ui->focus == FOCUS_PROMPT) {
        curs_set(1);
        move(LINES - 2, 2 + ui->len - off);
    } else {
        curs_set(0);
    }
    refresh();
}

/* ---------- input handling ---------- */

static void history_push(UI *ui)
{
    if (ui->nhist > 0 && strcmp(ui->hist[ui->nhist - 1], ui->input) == 0) {
        ui->hpos = ui->nhist;
        return;
    }
    if (ui->nhist == HIST_MAX) {
        memmove(ui->hist[0], ui->hist[1], sizeof ui->hist[0] * (HIST_MAX - 1));
        ui->nhist--;
    }
    memcpy(ui->hist[ui->nhist++], ui->input, INPUT_MAX);
    ui->hpos = ui->nhist;
}

static void history_move(UI *ui, int dir)
{
    int p = ui->hpos + dir;
    if (p < 0 || p > ui->nhist) return;
    ui->hpos = p;
    if (p == ui->nhist) ui->input[0] = '\0';
    else memcpy(ui->input, ui->hist[p], INPUT_MAX);
    ui->len = (int)strlen(ui->input);
}

static void on_key_prompt(UI *ui, int ch)
{
    switch (ch) {
    case '\n': case KEY_ENTER:
        if (ui->len > 0) {
            history_push(ui);
            run_command(ui, ui->input);
            ui->input[0] = '\0';
            ui->len = 0;
        }
        break;
    case KEY_BACKSPACE: case 127: case 8:
        if (ui->len > 0) ui->input[--ui->len] = '\0';
        break;
    case 21:                                 /* Ctrl-U: clear line */
        ui->input[0] = '\0';
        ui->len = 0;
        break;
    case 4:                                  /* Ctrl-D on an empty line: quit */
        if (ui->len == 0) ui->running = 0;
        break;
    case KEY_UP:   history_move(ui, -1); break;
    case KEY_DOWN: history_move(ui, +1); break;
    case '\t':     ui->focus = FOCUS_LIST; break;
    case 27:       ui->log_id = 0; break;
    default:
        if (ch >= 32 && ch < 127 && ui->len < INPUT_MAX - 1) {
            ui->input[ui->len++] = (char)ch;
            ui->input[ui->len] = '\0';
        }
        break;
    }
}

static void on_key_list(UI *ui, int ch, const Task *tasks, int n, int ok)
{
    switch (ch) {
    case 'q': case 'Q': ui->running = 0; break;
    case '\t':          ui->focus = FOCUS_PROMPT; break;
    case 27:            ui->log_id = 0; break;
    case KEY_UP:        if (ui->sel > 0) ui->sel--; break;
    case KEY_DOWN:      if (ui->sel < n - 1) ui->sel++; break;
    case '\n': case KEY_ENTER:
        if (ok && n > 0) ui->log_id = tasks[n - 1 - ui->sel].id;
        break;
    case 'x': case 'X':
        if (ok && n > 0) cmd_simple(ui, CMD_KILL, tasks[n - 1 - ui->sel].id);
        break;
    default: break;
    }
}

int dashboard_run(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    set_escdelay(25);                        /* Esc should not lag */
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

    static UI ui;                            /* zero-initialised */
    ui.running = 1;
    ui.focus = FOCUS_PROMPT;
    set_msg(&ui, "type 'help' for commands");

    Task *tasks = NULL;
    int n = 0;

    while (ui.running) {
        int ok = (fetch(&tasks, &n) == 0);
        if (ui.sel >= n) ui.sel = n > 0 ? n - 1 : 0;

        draw(&ui, tasks, n, ok);

        int ch = getch();
        if (ch != ERR && ch != KEY_RESIZE) {
            if (ui.focus == FOCUS_PROMPT) on_key_prompt(&ui, ch);
            else                          on_key_list(&ui, ch, tasks, n, ok);
        }
        free(tasks);
        tasks = NULL;
    }

    endwin();
    return 0;
}