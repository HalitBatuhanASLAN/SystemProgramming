#ifndef UTILS_H
#define UTILS_H

#include "common.h"

/* Hata mesajini stderr'e yazdirir */
void print_error(const char *msg);

/* Formatlı log mesaji stdout'a yazdirir (PID dahil) */
void log_msg(const char *fmt, ...);

/* fork() wrapper - hata kontrolu dahil */
pid_t safe_fork(void);

/* Rastgele sayi ureteci (0 ile max-1 arasi) */
int rand_range(int max);

#endif /* UTILS_H */