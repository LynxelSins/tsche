#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "common/task_types.h"

/* One connection = one request, one response. */

typedef enum {
    CMD_SUBMIT = 1,
    CMD_LIST,
    CMD_STATUS,
    CMD_KILL,
    CMD_SHUTDOWN
} Command;

typedef enum { RESP_OK = 0, RESP_ERR = 1 } RespCode;

typedef struct {
    int32_t cmd;                  /* Command */
    int32_t task_id;              /* STATUS / KILL */
    int32_t priority;             /* SUBMIT */
    char    command[MAX_CMD_LEN]; /* SUBMIT */
} Request;

/* Response = ResponseHeader followed by `count` Task structs. */
typedef struct {
    int32_t code;                 /* RespCode */
    int32_t value;                /* SUBMIT: new task id */
    int32_t count;                /* number of Task structs that follow */
    char    message[128];
} ResponseHeader;

/* Robust I/O helpers: return 0 on success, -1 on error/EOF. */
int protocol_read_full(int fd, void *buf, size_t n);
int protocol_write_full(int fd, const void *buf, size_t n);

/* Paths shared by daemon and client. */
void protocol_socket_path(char *buf, size_t n);
void protocol_log_dir(char *buf, size_t n);
void protocol_log_path(int task_id, char *buf, size_t n);

#endif
