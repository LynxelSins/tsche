#ifndef IPC_SERVER_H
#define IPC_SERVER_H

#include <signal.h>

int  ipc_server_start(const char *path);                         /* listening fd or -1 */
void ipc_server_run(int listen_fd, volatile sig_atomic_t *stop); /* loop until *stop */

#endif
