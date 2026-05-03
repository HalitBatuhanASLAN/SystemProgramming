#include "signal_state.h"

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t sigint_requested = 0;

static void handle_sigint(int signal_number)
{
    (void)signal_number;
    sigint_requested = 1;
}

int install_sigint_handler(void)
{
    struct sigaction action;

    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    return sigaction(SIGINT, &action, NULL) == 0;
}

int signal_state_sigint_requested(void)
{
    return sigint_requested != 0;
}
