#ifndef SIGNALS_HANDLER_H
#define SIGNALS_HANDLER_H

#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

/*
    global flags for IPC(inter process coommunication)
*/
extern volatile sig_atomic_t got_sigint;
extern volatile sig_atomic_t got_sigterm;
extern volatile sig_atomic_t workers_done;

#define MAX_EXPECTED_PIDS 8
extern volatile pid_t expected_pids[MAX_EXPECTED_PIDS];
extern volatile int expected_pid_statuses[MAX_EXPECTED_PIDS];
extern volatile sig_atomic_t expected_pid_count;

void setup_parent_signals(void);

void setup_worker_signals(void);

#endif