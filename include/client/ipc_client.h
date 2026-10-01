#ifndef IPC_CLIENT_H
#define IPC_CLIENT_H

#include "common/protocol.h"

/* Send one request, receive header (+ tasks). Returns 0 on success, -1 if the
 * daemon is unreachable or the exchange failed. *tasks_out (may be NULL) is
 * malloc'ed and must be free()d by the caller. */
int ipc_request(const Request *req, ResponseHeader *hdr, Task **tasks_out);
int ipc_ensure_daemon(void);

#endif
