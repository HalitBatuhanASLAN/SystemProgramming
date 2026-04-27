#ifndef WATCHDOG_H
#define WATCHDOG_H

/* =============================================================================
 * watchdog.h - Watchdog thread interface
 * =============================================================================
 *
 * A single thread inside the parent process. Its job is to print
 * periodic progress information to stderr so a human (or the test
 * harness) can verify the pipeline is making progress.
 *
 * Inputs:
 *   - one read end of a pipe per Reader process; Reader threads write
 *     "[R<i>] <count> lines processed\n" strings periodically;
 *   - a pointer to the parent's "children_alive" counter, updated by
 *     the parent every time waitpid() succeeds.
 *
 * Implementation uses select() with a 3-second timeout, so the loop
 * runs at most every 3 seconds even when no pipe traffic arrives.
 * ============================================================================= */

#include <signal.h>

/* Maximum number of Reader processes (and therefore pipes).               */
#define MAX_READERS 64

typedef struct
{
    int            pipe_read_ends[MAX_READERS];
    int            n_readers;
    char           log_names[MAX_READERS][256];

    /* Decremented by the parent main loop every time a child is reaped.
     * volatile because two threads (main + watchdog) read/write it. */
    volatile int*  children_alive;
} watchdog_arg_t;

/* Set to 1 by the parent to politely terminate the watchdog thread.       */
extern volatile sig_atomic_t shutdown_watchdog;

void* watchdog_thread_func(void* arg);

#endif /* WATCHDOG_H */