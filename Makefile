# Makefile
CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -D_POSIX_C_SOURCE=200809L -MMD -MP
INCS   = -Iinclude

.DELETE_ON_ERROR:

# -------------------------
# Person A sanity harness
# -------------------------
A_SRCS = src/prompt.c src/exec.c src/jobs.c tests/a_tests.c
A_OBJS = $(A_SRCS:.c=.o)
A_DEPS = $(A_OBJS:.o=.d)
A_BIN  = bin/a_tests

# -------------------------
# Person B sources + tests
# (parser/builtins/pipeline executor)
# -------------------------
B_SRCS     = src/parser.c src/builtins.c src/pipeline_exec.c
B_OBJS     = $(B_SRCS:.c=.o)
B_DEPS     = $(B_OBJS:.o=.d)

BTEST_SRCS = tests/b_tests.c
BTEST_OBJS = $(BTEST_SRCS:.c=.o)
BTEST_DEPS = $(BTEST_OBJS:.o=.d)
BTEST_BIN  = bin/b_tests

# -------------------------
# Person C tests (also need pipeline)
# -------------------------
CTEST_SRCS = tests/c_tests.c
CTEST_OBJS = $(CTEST_SRCS:.c=.o)
CTEST_DEPS = $(CTEST_OBJS:.o=.d)
CTEST_BIN  = bin/c_tests

.PHONY: all run btest ctest clean

# Default: build Person A harness
all: $(A_BIN)

bin:
	@mkdir -p bin

# Link rules
$(A_BIN): $(A_OBJS) | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $(A_OBJS)

$(BTEST_BIN): $(BTEST_OBJS) $(B_OBJS) src/exec.o src/jobs.o | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $^

$(CTEST_BIN): $(CTEST_OBJS) $(B_OBJS) src/exec.o src/jobs.o | bin
	$(CC) $(CFLAGS) $(INCS) -o $@ $^

# Compile rule
%.o: %.c
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

# Convenience targets
run: $(A_BIN)
	./$(A_BIN)

btest: $(BTEST_BIN)
	./$(BTEST_BIN)

ctest: $(CTEST_BIN)
	./$(CTEST_BIN)

# Clean everything
clean:
	rm -rf $(A_OBJS) $(B_OBJS) $(BTEST_OBJS) $(CTEST_OBJS) \
	       $(A_BIN) $(BTEST_BIN) $(CTEST_BIN) bin \
	       $(A_DEPS) $(B_DEPS) $(BTEST_DEPS) $(CTEST_DEPS)

# Include auto-generated header deps
-include $(A_DEPS) $(B_DEPS) $(BTEST_DEPS) $(CTEST_DEPS)
