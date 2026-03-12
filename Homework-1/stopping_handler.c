#include "stopping_handler.h"

#include <signal.h>  /*sigaction(), sigemptyset(), SIGINT — signal handling */
#include <stdio.h>   /*fprintf() — printing error messages to stderr */
#include <stdlib.h>  /*exit()    — terminating the program */

/*
    initialize the flag to 1, which means the program should continue running until it receives a SIGINT signal
*/
volatile sig_atomic_t continue_running = 1;

/*
    if a signal occurs, the signal handler will set the continue_running flag to 0, which will cause the main loop of the program to exit clear/flush resources
*/
static void handle_sigint(int signum)
{
    (void) signum;

    fprintf(stderr, "Received SIGINT, exitting program ...\n");
    continue_running = 0;
}

/*
    sigaction is much more better definiton and consistnt on Posix so I prefer it instead of signal
*/
void setup_stopping_handler()
{
    struct sigaction sa;
    /*setting our handelr functin for SIGINT*/
    sa.sa_handler = handle_sigint;
    /*initializing the signal mask empty and not blocking any signals*/
    sigemptyset(&sa.sa_mask);
    /*no special flags neede*/
    sa.sa_flags = 0;

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        fprintf(stderr, "Error: Failed to set up signal handler for SIGINT\n");
        exit(1);
    }
}