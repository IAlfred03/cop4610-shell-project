#pragma once
#include <sys/types.h>

/* Initialize the job table. Call once at startup. */
void jobs_init(void);

/* Monotonic, never-reused job ids starting at 1. */
int  jobs_next_id(void);

/*
 * Register a background job:
 *  - job_id: call jobs_next_id() at launch
 *  - pid:    for pipelines, pass the PID of the LAST stage (Person B provides it)
 *  - cmdline: original command line to print when the job completes
 *
 * On registration, prints:   [Job] PID
 * On completion (reaped), prints: [Job] + done CMDLINE
 */

void jobs_register(int job_id, pid_t pid, const char *cmdline);

/* Non-blocking reap; call once per REPL tick to print completed jobs. */
void jobs_mark_done_nonblocking(void);

/* Block until all active background jobs complete (used by builtin exit). */
void jobs_wait_all(void);

/* Print active background jobs for the `jobs` builtin. */
void jobs_print_active(void);