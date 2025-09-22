CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L
INCS   = -Iinclude

# main shell (keep your team's sources; update if your repo uses different ones)
SHELL_SRCS = src/main.c src/exec.c src/jobs.c src/prompt.c
SHELL_OBJS = $(SHELL_SRCS:.c=.o)
SHELL_BIN  = bin/shell

# Person A sanity harness
A_SRCS = src/prompt.c src/exec.c src/jobs.c tests/a_tests.c
A_OBJS = $(A_SRCS:.c=.o)
A_BIN  = bin/a_tests

.PHONY: all run run-a clean

all: $(SHELL_BIN) $(A_BIN)

bin:
	@mkdir -p bin

# main shell build
$(SHELL_BIN): $(SHELL_OBJS) | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $(SHELL_OBJS)

# Person A harness build
$(A_BIN): $(A_OBJS) | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $(A_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# convenience
run: $(SHELL_BIN)
	./$(SHELL_BIN)

run-a: $(A_BIN)
	./$(A_BIN)

clean:
	rm -f $(SHELL_OBJS) $(A_OBJS) $(SHELL_BIN) $(A_BIN)
