#include "watchdog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>

volatile sig_atomic_t shutdown_watchdog = 0;

void* watchdog_thread_func(void* arg) {
    watchdog_arg_t* wa    = (watchdog_arg_t*)arg;
    int             n     = wa->n_readers;
    long            lines[MAX_READERS];
    time_t          start = time(NULL);

    memset(lines, 0, sizeof(lines));

    while (!shutdown_watchdog) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = 0;
        for (int i = 0; i < n; i++) {
            if (wa->pipe_read_ends[i] >= 0) {
                FD_SET(wa->pipe_read_ends[i], &rfds);
                if (wa->pipe_read_ends[i] > maxfd)
                    maxfd = wa->pipe_read_ends[i];
            }
        }

        struct timeval tv = {3, 0};
        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("[WATCHDOG] select");
            break;
        }

        if (ret > 0) {
            for (int i = 0; i < n; i++) {
                if (wa->pipe_read_ends[i] >= 0 &&
                    FD_ISSET(wa->pipe_read_ends[i], &rfds)) {
                    char buf[256];
                    ssize_t nr;
                    while ((nr = read(wa->pipe_read_ends[i],
                                      buf, sizeof(buf)-1)) > 0) {
                        buf[nr] = '\0';
                        /* Format: "[R<i>] <n> lines processed\n" */
                        long nl;
                        if (sscanf(buf, "[R%*d] %ld lines", &nl) == 1)
                            lines[i] = nl;
                    }
                }
            }
        }

        /* Her select dönüşünde (3s veya data) özet yaz */
        long elapsed = (long)(time(NULL) - start);
        fprintf(stderr, "[WATCHDOG] Progress at T+%lds:", elapsed);
        for (int i = 0; i < n; i++)
            fprintf(stderr, " Reader%d(%s)=%ld",
                    i, wa->log_names[i], lines[i]);
        fprintf(stderr, "\n");
        fflush(stderr);
    }

    return NULL;
}
