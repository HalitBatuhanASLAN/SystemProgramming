/* watchdog.c - Periodic progress watcher
 *
 * Algorithm:
 *   while !shutdown_watchdog:
 *       select(pipe_read_ends..., timeout = 3 seconds)
 *       if any pipe became readable:
 *           drain it, parse "[R<i>] <count> lines" lines, keep latest count
 *       if 3 seconds since last snapshot, OR shutdown requested:
 *           emit "[WATCHDOG] Progress at T+<elapsed>s: Reader0=<n>..." to stderr
 *
 * stderr is used (not stdout) per the assignment.
*/

#include "watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>

volatile sig_atomic_t shutdown_watchdog = 0;

void* watchdog_thread_func(void* arg)
{
    watchdog_arg_t* wa = (watchdog_arg_t*)arg;
    int n = wa->n_readers;
    long lines[MAX_READERS];
    time_t start = time(NULL);
    time_t last_snapshot = start;

    memset(lines, 0, sizeof(lines));

    /* Main loop: select on all reader pipes with a 3-second timeout. */
    while(!shutdown_watchdog)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        for(int i = 0; i < n; i++)
        {
            if(wa->pipe_read_ends[i] >= 0)
            {
                FD_SET(wa->pipe_read_ends[i], &rfds);
                if (wa->pipe_read_ends[i] > maxfd)
                    maxfd = wa->pipe_read_ends[i];
            }
        }

        struct timeval tv = {3, 0};   /* 3-second tick */
        int ret = (maxfd >= 0) ? select(maxfd + 1, &rfds, NULL, NULL, &tv) : (sleep(3), 0);

        if(ret < 0)
        {
            if(errno == EINTR)
                continue;
            perror("[WATCHDOG] select");
            break;
        }

        /* Drain pipes that have data. */
        if(ret > 0)
        {
            for(int i = 0; i < n; i++)
            {
                if(wa->pipe_read_ends[i] >= 0 && FD_ISSET(wa->pipe_read_ends[i], &rfds))
                {
                    char buf[1024];
                    ssize_t nr;
                    while((nr = read(wa->pipe_read_ends[i], buf, sizeof(buf) - 1)) > 0)
                    {
                        buf[nr] = '\0';

                        /* Multiple "[R<i>] <count> lines" strings may have been concatenated. Walk newline by newline and remember the largest count. */
                        char* p = buf;
                        long latest = lines[i];
                        while(p && *p)
                        {
                            long nl;
                            if(sscanf(p, "[R%*d] %ld lines", &nl) == 1)
                            {
                                if(nl > latest)
                                    latest = nl;
                            }
                            char* nxt = strchr(p, '\n');
                            if(!nxt)
                                break;
                            p = nxt + 1;
                        }
                        lines[i] = latest;
                    }
                }
            }
        }

        /* Emit a snapshot every 3 seconds (or immediately on shutdown). */
        time_t now = time(NULL);
        if(now - last_snapshot >= 3 || shutdown_watchdog)
        {
            last_snapshot = now;
            long elapsed = (long)(now - start);

            fprintf(stderr, "[WATCHDOG] Progress at T+%lds:", elapsed);
            for (int i = 0; i < n; i++)
                fprintf(stderr, " Reader%d(%s)=%ld", i, wa->log_names[i], lines[i]);
            
            int alive = wa->children_alive ? *wa->children_alive : -1;
            if(alive >= 0)
                fprintf(stderr, " children_alive=%d", alive);
            fprintf(stderr, "\n");
            fflush(stderr);
        }
    }

    return NULL;
}