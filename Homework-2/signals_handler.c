#include "signals_handler.h"

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

/*
    flag definitions
*/
volatile sig_atomic_t got_sigint   = 0;
volatile sig_atomic_t got_sigterm  = 0;
volatile sig_atomic_t workers_done = 0;

volatile pid_t expected_pids[MAX_EXPECTED_PIDS];
volatile int expected_pid_statuses[MAX_EXPECTED_PIDS];
volatile sig_atomic_t expected_pid_count = 0;

/*
    as we have to use write(can not use fprintf and snprintf as to prevent deadlocks) we must convert integersn to characters
*/
static int int_to_str(int val, char *buf, int buf_size)
{
    int neg = 0;
    char tmp[16];
    int pos = 0;

    if(val < 0)
    {
        neg = 1;
        val = -val;
    }
    if(val == 0)
        tmp[pos++] = '0';
    else
    {
        while(val > 0 && pos < 15)
        {
            tmp[pos++] = '0' + (val % 10);
            val /= 10;
        }
    }

    int total = neg + pos;
    if(total >= buf_size) return 0;

    int idx = 0;
    if(neg)
        buf[idx++] = '-';
    for(int i = pos - 1; i >= 0; i--)
        buf[idx++] = tmp[i];
    buf[idx] = '\0';
    return idx;
}


/*handler for SIGUSER!(parent)*/
static void sigusr1_handler(int sig, siginfo_t *info, void *ucontext)
{
    (void)sig;
    (void)ucontext;

    /*keep sender PID*/
    if (expected_pid_count < MAX_EXPECTED_PIDS)
        expected_pids[expected_pid_count++] = info->si_pid;

    workers_done++;
}

/*only parent can be use, just works when ctrl^c pressed */
static void sigint_handler(int sig)
{
    (void)sig;
    got_sigint = 1;
}

/*checks if a given PID is in the list of workers that sent SIGUSR1*/
static int get_expected_pid_index(pid_t pid)
{
    for (int i = 0; i < expected_pid_count; i++)
    {
        if(expected_pids[i] == pid)
            return i;
    }
    return -1;
}

/*when a cild dies kernel send SIGCHILD to parent*/
static void sigchld_handler(int sig)
{
    (void)sig;

    int saved_errno = errno;
    int status;
    pid_t pid;

    
    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int p_idx = get_expected_pid_index(pid);

        /*when worker finished normally*/
        if (p_idx >= 0)
        {
            expected_pid_statuses[p_idx] = status;
            continue;
        }

        /*unexpected child death*/
        char buf[128];
        int idx = 0;
        const char *p1 = "[Parent] Worker PID: ";
        const char *p2 = " terminated unexpectedly (exit status: ";
        const char *p3 = ").\n";

        int len1 = 0;
        while (p1[len1]) len1++;
        for (int i = 0; i < len1 && idx < 126; i++)
            buf[idx++] = p1[i];

        char numbuf[16];
        int nlen = int_to_str((int)pid, numbuf, sizeof(numbuf));
        for (int i = 0; i < nlen && idx < 126; i++)
            buf[idx++] = numbuf[i];

        int len2 = 0;
        while (p2[len2]) len2++;
        for (int i = 0; i < len2 && idx < 126; i++)
            buf[idx++] = p2[i];

        int exit_val = 0;
        if (WIFEXITED(status)) {
            exit_val = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            exit_val = WTERMSIG(status);
        }

        nlen = int_to_str(exit_val, numbuf, sizeof(numbuf));
        for (int i = 0; i < nlen && idx < 126; i++)
            buf[idx++] = numbuf[i];

        int len3 = 0;
        while (p3[len3]) len3++;
        for (int i = 0; i < len3 && idx < 127; i++)
            buf[idx++] = p3[i];

        write(STDERR_FILENO, buf, idx);
    
    }

    errno = saved_errno;
}

/*just for child worker*/
static void sigterm_handler(int sig)
{
    (void)sig;
    got_sigterm = 1;
}

/*configures parent signals*/
void setup_parent_signals(void)
{
    struct sigaction sa;

    /* SIGUSR1: Worker completion notification. Needs SA_SIGINFO to read sender PID. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1)
    {
        write(STDERR_FILENO, "sigaction SIGUSR1 failed\n", 25);
        _exit(1);
    }

    /* * SIGINT: Ctrl+C trigger. 
     * NO SA_RESTART here! We want pause() and waitpid() to be actively interrupted 
     * with EINTR so the main loop can break immediately and handle the shutdown.
     */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        write(STDERR_FILENO, "sigaction SIGINT failed\n", 24);
        _exit(1);
    }

    /* SIGCHLD: Zombie prevention */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = sigchld_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        write(STDERR_FILENO, "sigaction SIGCHLD failed\n", 25);
        _exit(1);
    }
}

/*configure worker signals*/
void setup_worker_signals(void)
{
    struct sigaction sa;

    /* SIGTERM: Instructs the worker to stop searching, write partial logs, and exit. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1)
    {
        write(STDERR_FILENO, "sigaction SIGTERM failed\n", 25);
        _exit(1);
    }

    /* Reset SIGUSR1 and SIGCHLD to default so child doesn't execute parent's handlers */
    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore SIGINT (Ctrl+C). The Parent will handle it and send SIGTERM to workers instead. */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);
}