#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <signal.h>

#define MAX_READERS 64

typedef struct {
    int  pipe_read_ends[MAX_READERS];
    int  n_readers;
    char log_names[MAX_READERS][256];
} watchdog_arg_t;

extern volatile sig_atomic_t shutdown_watchdog;

void* watchdog_thread_func(void* arg);

#endif /* WATCHDOG_H */
