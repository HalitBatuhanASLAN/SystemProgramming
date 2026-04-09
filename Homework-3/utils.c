#include "utils.h"
#include <stdarg.h>

void print_error(const char *msg) {
    fprintf(stderr, "[PID:%ld] Error: %s: %s\n", (long)getpid(), msg, strerror(errno));
}

void log_msg(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    /* PID prefix ekle */
    printf("[PID:%ld] ", (long)getpid());
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);
    va_end(args);
}

pid_t safe_fork(void) {
    fflush(NULL); /* Fork oncesi buffered output'u temizle */
    pid_t pid = fork();
    if (pid == -1) {
        print_error("fork failed");
        exit(EXIT_FAILURE);
    }
    return pid;
}

int rand_range(int max) {
    if (max <= 0) return 0;
    return rand() % max;
}
