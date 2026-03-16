#ifndef SIGNALS_HANDLER_H
#define SIGNALS_HANDLER_H

#include <signal.h>    /* sigaction(), sig_atomic_t */
#include <sys/wait.h>  /* waitpid(), WNOHANG        */

/*
 * Flag'ler — extern: hem signals.c hem main.c/worker.c görebilir
 *
 * volatile: compiler register'a almasın (TLPI sayfa 472)
 * sig_atomic_t: atomik okuma/yazma garantisi (TLPI sayfa 472)
 */
extern volatile sig_atomic_t got_sigint;   /* SIGINT geldi mi?          */
extern volatile sig_atomic_t got_sigterm;  /* SIGTERM geldi mi?         */
extern volatile sig_atomic_t workers_done; /* Kaç worker SIGUSR1 gönderdi */

void setup_parent_signals(void);

void setup_worker_signals(void);

#endif