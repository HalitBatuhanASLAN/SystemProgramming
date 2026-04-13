/*
 * PURPOSE:
 * Implements four small utility functions shared across all modules: error reporting, log output, safe fork, and bounded random number generation.
 */

#include "utils.h"
#include <stdarg.h>   /* va_list, va_start, va_end, vprintf */

/*
 * print_error
 * Using getpid() at call time means the message always reflects the actual process that encountered the error, even after multiple fork() calls.
 * ============================================================ */
void print_error(const char *msg)
{
    fprintf(stderr, "[PID:%ld] Error: %s: %s\n",
            (long)getpid(), msg, strerror(errno));
}

/* log_msg
 * Prints a formatted informational log line to stdout.
 * WHY fflush?
 * When multiple processes write to the same stdout (which happens here because all children inherit the parent's  file descriptor) the C library may buffer output. An explicit fflush() after each log line prevents messages from being reordered or lost when the process exits.
*/
void log_msg(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* PID prefix so every line is identifiable in mixed output. */
    printf("[PID:%ld] ", (long)getpid());

    /* Format the caller's message using the variadic arguments. */
    vprintf(fmt, args);

    printf("\n");
    fflush(stdout); /* Flush immediately; don't rely on buffering. */

    va_end(args);
}

/* A thin wrapper around fork() that:
 * 1. Flushes all buffered stdio streams with fflush(NULL) so the child does not inherit unflushed data and potentially print it twice.
 * 2. Calls print_error() and exit() if fork() fails, so the caller can treat the return value as always valid (> 0 in parent, == 0 in child).
*/
pid_t safe_fork(void)
{
    /*
     * fflush(NULL) flushes ALL open output streams.
     * This must be done BEFORE fork() because after fork() both the parent and child share copies of the stdio buffers;if the buffer is not empty it would be flushed twice.
     */
    fflush(NULL);

    pid_t pid = fork();
    if(pid == -1)
    {
        /* fork() failed: print the reason and abort this process. */
        print_error("fork failed");
        exit(EXIT_FAILURE);
    }
    return pid;
}

/* The guard against max <= 0 prevents a division-by-zero from the modulo operator and ensures the function is safe to call even when a floor count or word count is zero (which should not happen after validation, but defensive coding is prudent).
*/
int rand_range(int max)
{
    if(max <= 0)
        return 0;  /* Defensive: avoid modulo by zero. */
    return rand() % max;
}