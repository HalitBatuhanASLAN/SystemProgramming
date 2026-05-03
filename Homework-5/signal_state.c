#include "signal_state.h"

#include <signal.h>
#include <stddef.h>

static volatile sig_atomic_t sigint_requested = 0;

/*
 * Minimal SIGINT handler. It only stores a flag, because printing, locking, or
 * freeing memory from a signal handler would not be safe here.
 */
static void handle_sigint(int signal_number)
{
    (void)signal_number;
    /* Only set a flag here; the normal code path performs shutdown. */
    sigint_requested = 1;
}

/*
 * Registers the SIGINT handler used by the program. After this, Ctrl+C does
 * not directly kill the process; the main loop notices the flag and shuts down.
 */
int install_sigint_handler(void)
{
    struct sigaction action;

    action.sa_handler = handle_sigint;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    return sigaction(SIGINT, &action, NULL) == 0;
}

/*
 * Returns whether SIGINT has been received. Other modules use this instead of
 * reading the global flag directly.
 */
int signal_state_sigint_requested(void)
{
    return sigint_requested != 0;
}
