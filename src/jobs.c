#include "jobs.h"
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

#define MAX_JOBS 32

typedef struct {
    int   id;
    pid_t pid;            /* For pipeline background: PID of the LAST stage */
    int   active;         /* 1 = active, 0 = free/done */
    char  cmd[256];       /* store original command line (<= 200 chars spec) */
} job_t;

static job_t JOBS[MAX_JOBS];
static int next_id = 1;

void jobs_init(void){
    memset(JOBS, 0, sizeof(JOBS));
    next_id = 1;
}

int jobs_next_id(void){
    return next_id;
}

void jobs_register(int job_id, pid_t pid, const char *cmdline){
    for (int i = 0; i < MAX_JOBS; ++i){
        if (!JOBS[i].active){
            JOBS[i].id = job_id;
            JOBS[i].pid = pid;
            JOBS[i].active = 1;
            snprintf(JOBS[i].cmd, sizeof(JOBS[i].cmd), "%s", cmdline ? cmdline : "");
            printf("[%d] %d\n", job_id, (int)pid);
            fflush(stdout);
            if (job_id >= next_id) next_id = job_id + 1;
            return;
        }
    }
    fprintf(stderr, "job table full\n");
}

/* Reap all finished children without blocking; print completion notices. */
void jobs_mark_done_nonblocking(void){
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
        for (int i = 0; i < MAX_JOBS; ++i){
            if (JOBS[i].active && JOBS[i].pid == pid){
                printf("[%d] + done %s\n", JOBS[i].id, JOBS[i].cmd);
                fflush(stdout);
                JOBS[i].active = 0;
            }
        }
    }
}

/* Polling loop (signals not required by spec). */
void jobs_wait_all(void){
    int any;
    do {
        any = 0;
        for (int i = 0; i < MAX_JOBS; ++i){
            if (JOBS[i].active){ any = 1; break; }
        }
        if (any){
            jobs_mark_done_nonblocking();
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 50 * 1000 * 1000; // 50 ms
            nanosleep(&ts, NULL);

        }
    } while (any);
}

void jobs_print_active(void){
    int any = 0;
    for (int i = 0; i < MAX_JOBS; ++i){
        if (JOBS[i].active){
            any = 1;
            printf("[%d]+ %d %s\n", JOBS[i].id, (int)JOBS[i].pid, JOBS[i].cmd);
        }
    }
    if (!any) {
        printf("no active background processes\n");
    }
    fflush(stdout);
}
