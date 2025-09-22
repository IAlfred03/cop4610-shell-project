CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L
INCS   = -Iinclude

# Person A sanity harness only
A_SRCS = src/prompt.c src/exec.c src/jobs.c tests/a_tests.c
A_OBJS = $(A_SRCS:.c=.o)
A_BIN  = bin/a_tests

.PHONY: all run clean shell

# default: build only the harness
all: $(A_BIN)

bin:
	@mkdir -p bin

$(A_BIN): $(A_OBJS) | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $(A_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

run: $(A_BIN)
	./$(A_BIN)

# optional shell build (will still fail until parser.h/builtins.h exist)
shell:
	@echo "Shell build disabled until parser.h and builtins.h are present."

clean:
	rm -f $(A_OBJS) $(A_BIN)
