/*
 * Declares small utility functions used throughout every module in the system.  These are general-purpose helpers that do not belong to any single subsystem.
 */

#ifndef UTILS_H
#define UTILS_H

#include "common.h"   /* pid_t, standard headers */

/* print_error – write a formatted error message to stderr. */
void print_error(const char *msg);

/* log_msg – write a formatted informational log line to stdout. */
void log_msg(const char *fmt, ...);

/*
 * safe_fork – call fork() and abort the process on failure.
 * Flushes all buffered output streams before forking so that partially-buffered data is not duplicated in the child.
 */
pid_t safe_fork(void);

/*
 * rand_range – return a uniformly distributed random integer in
 * the range [0, max).
 */
int rand_range(int max);

#endif