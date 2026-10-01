# Fedora: sudo dnf install gcc make ncurses-devel
CC       ?= gcc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=gnu11 -Wall -Wextra -pthread
CPPFLAGS := -Iinclude -D_GNU_SOURCE -MMD -MP

COMMON_SRC := src/common/task_types.c src/common/protocol.c
DAEMON_SRC := src/daemon/main.c src/daemon/ipc_server.c \
              src/daemon/process_manager.c src/daemon/scheduler.c
CLIENT_SRC := src/client/main.c src/client/ipc_client.c src/tui/dashboard.c

DAEMON_OBJ := $(patsubst src/%.c,build/%.o,$(COMMON_SRC) $(DAEMON_SRC))
CLIENT_OBJ := $(patsubst src/%.c,build/%.o,$(COMMON_SRC) $(CLIENT_SRC))

.PHONY: all clean demo
all: bin/tasksched bin/sche

bin/tasksched: $(DAEMON_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ -pthread

bin/sche: $(CLIENT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ -lncurses

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

demo: all
	./scripts/demo_workload.sh

clean:
	rm -rf build bin

-include $(DAEMON_OBJ:.o=.d) $(CLIENT_OBJ:.o=.d)
