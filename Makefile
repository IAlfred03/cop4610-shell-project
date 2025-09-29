CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L
INCS   = -Iinclude

# Person A sanity harness only
A_SRCS = src/prompt.c src/exec.c src/jobs.c tests/a_tests.c
A_OBJS = $(A_SRCS:.c=.o)
A_BIN  = bin/a_tests

# Person B sources + test harness
B_SRCS      = src/parser.c src/builtins.c src/pipeline_exec.c
BTEST_SRCS  = tests/b_tests.c
BTEST_OBJS  = $(BTEST_SRCS:.c=.o)
BTEST_BIN   = bin/b_tests

.PHONY: all run clean full

# Person C sources + test harness
C_SRCS     = src/parser.c src/builtins.c
CTEST_SRCS = tests/c_tests.c
CTEST_OBJS = $(CTEST_SRCS:.c=.o)
CTEST_BIN  = bin/c_tests

$(CTEST_BIN): $(CTEST_OBJS) $(C_SRCS:.c=.o) src/exec.o src/jobs.o | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $^

.PHONY: ctest
ctest: $(CTEST_BIN)
	./$(CTEST_BIN)


# default: build only the harness
all: $(A_BIN)

bin:
	@mkdir -p bin

$(A_BIN): $(A_OBJS) | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $(A_OBJS)

# Build Person B test binary (links A launcher + jobs + B code)
$(BTEST_BIN): $(BTEST_OBJS) $(B_SRCS:.c=.o) src/exec.o src/jobs.o | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

run: $(A_BIN)
	./$(A_BIN)

.PHONY: btest
btest: $(BTEST_BIN)
	./$(BTEST_BIN)

# optional full shell build (enable later when these files exist)
# FULL_SRCS = $(A_SRCS) src/parser.c src/builtins.c src/pipeline_exec.c
# FULL_OBJS = $(FULL_SRCS:.c=.o)
# FULL_BIN  = bin/shell
# full: $(FULL_BIN)
# $(FULL_BIN): $(FULL_OBJS) | bin
# 	$(CC) $(CFLAGS) $(INCS) -o $@ $(FULL_OBJS)

clean:
	rm -rf $(A_OBJS) $(A_BIN) bin
