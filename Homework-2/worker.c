#include "worker.h"

#include <stdio.h>    /* printf(), fprintf()        */
#include <stdlib.h>   /* exit()                     */
#include <unistd.h>   /* fork(), getpid(), getppid()*/
#include <signal.h>   /* kill()                     */
#include <sys/wait.h> /* waitpid()                  */
#include <errno.h>    /* errno                      */

void launch_workers(Worker_Partition  partitions[MAX_WORKERS],
                    int               num_of_workers,
                    const char       *pattern,
                    long              min_size,
                    Worker_Result     worker_results[MAX_WORKERS])
{
    for (int i = 0; i < num_of_workers; i++) {
        int worker_id = i;
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            setup_worker_signals();

            Searching_Result result;
            init_searching_result(&result);

            for (int j = 0; j < partitions[worker_id].num_of_subdirectories; j++) {
                if (got_sigterm) {
                    printf("[Worker PID:%d] SIGTERM received."
                           " Partial matches: %d. Exiting.\n",
                           (int)getpid(), result.match_count);
                    exit(result.match_count % 256);
                }
                search_directory(partitions[worker_id].directories[j],
                                 pattern,
                                 min_size,
                                 &result,
                                 1);        /* verbose=1: MATCH yazdır */
            }

            kill(getppid(), SIGUSR1);
            exit(result.match_count % 256);
        }

        worker_results[i].pid         = pid;
        worker_results[i].match_count = 0;
    }
}

void wait_for_workers(int           num_of_workers,
                      Worker_Result worker_results[MAX_WORKERS])
{
    while (workers_done < num_of_workers) {
        if (got_sigint) {
            printf("[Parent] SIGINT received."
                   " Terminating all workers...\n");
            fflush(stdout);
            kill_workers(num_of_workers, worker_results);
            return;
        }
        pause();
    }

    /* pause() döndükten sonra SIGINT kontrolü — döngü bitmeden önce */
    if (got_sigint) {
        printf("[Parent] SIGINT received."
               " Terminating all workers...\n");
        fflush(stdout);
        kill_workers(num_of_workers, worker_results);
        return;
    }

    for (int i = 0; i < num_of_workers; i++) {
        int status;
        pid_t pid = waitpid(worker_results[i].pid, &status, 0);
        if (pid == -1) {
            if (errno == ECHILD) continue;
            perror("waitpid");
            continue;
        }
        if (WIFEXITED(status))
            worker_results[i].match_count = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            worker_results[i].match_count = 0;
    }
}

void kill_workers(int           num_of_workers,
                  Worker_Result worker_results[MAX_WORKERS])
{
    for (int i = 0; i < num_of_workers; i++) {
        if (worker_results[i].pid > 0)
            kill(worker_results[i].pid, SIGTERM);
    }

    sleep(3);

    for (int i = 0; i < num_of_workers; i++) {
        if (worker_results[i].pid > 0)
            kill(worker_results[i].pid, SIGKILL);
    }

    for (int i = 0; i < num_of_workers; i++) {
        if (worker_results[i].pid <= 0) continue;  /* zaten toplandı */
        int status;
        waitpid(worker_results[i].pid, &status, 0);
        worker_results[i].pid = 0;
    }
}