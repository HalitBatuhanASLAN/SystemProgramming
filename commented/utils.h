/*
 * ============================================================
 * FILE: utils.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Declares small utility functions used throughout every
 *   module in the system.  These are general-purpose helpers
 *   that do not belong to any single subsystem.
 *
 * FUNCTIONS PROVIDED:
 *   print_error – formatted error message to stderr with PID.
 *   log_msg     – formatted informational log to stdout with PID.
 *   safe_fork   – fork() with automatic error handling.
 *   rand_range  – bounded random integer [0, max).
 * ============================================================
 */

#ifndef UTILS_H
#define UTILS_H

#include "common.h"   /* pid_t, standard headers */

/*
 * print_error – write a formatted error message to stderr.
 *
 * Prepends the calling process's PID so log lines from different
 * processes can be distinguished in the terminal output.
 * Automatically appends strerror(errno) so the OS reason for
 * the failure is always visible.
 *
 * Parameters:
 *   msg – short description of the failing operation
 *         (e.g., "fork failed", "mmap failed").
 *
 * Example output:
 *   [PID:1234] Error: fork failed: No child processes
 */
void print_error(const char *msg);

/*
 * log_msg – write a formatted informational log line to stdout.
 *
 * Prepends the calling process's PID, then formats the rest of
 * the message exactly like printf(), and appends a newline.
 * Calls fflush(stdout) so the line appears immediately even when
 * stdout is line-buffered or fully buffered (which can happen
 * when output is piped to a file or another process).
 *
 * Parameters:
 *   fmt  – printf-style format string.
 *   ...  – variadic arguments matching the format string.
 *
 * Example output:
 *   [PID:3003] Letter-carrier-process_0 selected char 'a' of word 101
 */
void log_msg(const char *fmt, ...);

/*
 * safe_fork – call fork() and abort the process on failure.
 *
 * Flushes all buffered output streams before forking so that
 * partially-buffered data is not duplicated in the child.
 * If fork() returns -1 (error) the function prints an error
 * message and calls exit(EXIT_FAILURE) immediately so the
 * caller never has to check the return value for -1.
 *
 * Returns:
 *   > 0  – in the parent: PID of the newly created child.
 *   == 0 – in the child: always zero.
 *   (never returns -1; aborts instead)
 */
pid_t safe_fork(void);

/*
 * rand_range – return a uniformly distributed random integer in
 *              the range [0, max).
 *
 * Uses the standard rand() function (which must be seeded with
 * srand() before use; main() seeds it with time(NULL)^getpid()).
 *
 * Parameters:
 *   max – exclusive upper bound.  If max <= 0, returns 0 safely.
 *
 * Used by:
 *   - letter_carrier_run() to pick a random character task.
 *   - request_reposition_elevator() to choose a random target floor.
 */
int rand_range(int max);

#endif /* UTILS_H */
