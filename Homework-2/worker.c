#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

void launch_workers(Worker_Partition  partitions[MAX_WORKERS],int num_of_workers,const char *pattern, long min_size,Worker_Result worker_results[MAX_WORKERS])
{
    for (int i = 0; i < num_of_workers; i++)
    {
        int worker_id = i;
        pid_t pid = fork();

        if(pid == -1)
        {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if(pid == 0)
        {
            /* ── CHILD PROCESS ── */
            setup_worker_signals();

            char worker_file_name[256];
            snprintf(worker_file_name, sizeof(worker_file_name),
                    "/tmp/worker_%d.txt", (int)getpid());

            /*tmp file to write mathes*/
            char tmp_file_name[256];
            snprintf(tmp_file_name, sizeof(tmp_file_name),
                    "/tmp/worker_%d_tmp.txt", (int)getpid());

            FILE *worker_file = fopen(tmp_file_name, "w");
            if (worker_file == NULL)
                fprintf(stderr, "Error: cannot open '%s'\n", tmp_file_name);

            Searching_Result result;
            init_searching_result(&result);

            for (int j = 0; j < partitions[worker_id].num_of_subdirectories; j++)
            {
                if (got_sigterm) {
                    printf("[Worker PID:%d] SIGTERM received."
                        " Partial matches: %d. Exiting.\n",
                        (int)getpid(), result.match_count);
                    fflush(stdout);
                    if (worker_file != NULL) {
                        fflush(worker_file);
                        fclose(worker_file);
                    }

                    /*for sigterm siruation write final result*/
                    FILE *final = fopen(worker_file_name, "w");
                    if (final != NULL) {
                        fprintf(final, "Scanned:%d\n", result.scan_count);
                        
                        FILE *tmp = fopen(tmp_file_name, "r");
                        if (tmp != NULL)
                        {
                            char line[4096];
                            while (fgets(line, sizeof(line), tmp))
                                fputs(line, final);
                            fclose(tmp);
                        }
                        fclose(final);
                    }
                    remove(tmp_file_name);
                    exit(result.match_count % 256);
                }

                search_directory(partitions[worker_id].directories[j],
                                pattern, min_size, &result, 1, worker_file);
            }

            if (got_sigterm)
            {
                printf("[Worker PID:%d] SIGTERM received."
                    " Partial matches: %d. Exiting.\n",
                    (int)getpid(), result.match_count);
                fflush(stdout);
                if (worker_file != NULL) {
                    fflush(worker_file);
                    fclose(worker_file);
                }
                FILE *final = fopen(worker_file_name, "w");
                if (final != NULL)
                {
                    fprintf(final, "Scanned:%d\n", result.scan_count);
                    FILE *tmp = fopen(tmp_file_name, "r");
                    if (tmp != NULL) {
                        char line[4096];
                        while (fgets(line, sizeof(line), tmp))
                            fputs(line, final);
                        fclose(tmp);
                    }
                    fclose(final);
                }
                remove(tmp_file_name);
                exit(result.match_count % 256);
            }

            /*normal finisgh*/
            if (worker_file != NULL) {
                fflush(worker_file);
                fclose(worker_file);
            }

            FILE *final = fopen(worker_file_name, "w");
            if (final != NULL)
            {
                fprintf(final, "Scanned:%d\n", result.scan_count);
                FILE *tmp = fopen(tmp_file_name, "r");
                if (tmp != NULL) {
                    char line[4096];
                    while (fgets(line, sizeof(line), tmp))
                        fputs(line, final);
                    fclose(tmp);
                }
                fclose(final);
            }
            remove(tmp_file_name);

            kill(getppid(), SIGUSR1);
            exit(result.match_count % 256);
        }

        /* ── PARENT PROCESS ── */
        worker_results[i].pid = pid;
        worker_results[i].match_count = 0;
    }
}

void wait_for_workers(int num_of_workers,Worker_Result worker_results[MAX_WORKERS])
{
    while(workers_done < num_of_workers)
    {
        if (got_sigint) {
            printf("[Parent] SIGINT received."
                   " Terminating all workers...\n");
            fflush(stdout);
            kill_workers(num_of_workers, worker_results);
            return;
        }
        pause();
    }

    if (got_sigint)
    {
        printf("[Parent] SIGINT received."
               " Terminating all workers...\n");
        fflush(stdout);
        kill_workers(num_of_workers, worker_results);
        return;
    }

    for (int i = 0; i < num_of_workers; i++)
    {
        pid_t target_pid = worker_results[i].pid;
        int status;
        int found_in_handler = 0;

        for (int j = 0; j < expected_pid_count; j++)
        {
            if (expected_pids[j] == target_pid)
            {
                status = expected_pid_statuses[j];
                if (WIFEXITED(status))
                    worker_results[i].match_count = WEXITSTATUS(status);
                else
                    worker_results[i].match_count = 0;
                
                found_in_handler = 1;
                break;
            }
        }

        if(!found_in_handler)
        {
            pid_t p = waitpid(target_pid, &status, WNOHANG);
            if (p > 0)
            {
                if (WIFEXITED(status))
                    worker_results[i].match_count = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    worker_results[i].match_count = 0;
            }
            else
                worker_results[i].match_count = 0;
        }
    }
}

void kill_workers(int num_of_workers,Worker_Result worker_results[MAX_WORKERS])
{
    for (int i = 0; i < num_of_workers; i++)
    {
        if (worker_results[i].pid > 0)
            kill(worker_results[i].pid, SIGTERM);
    }

    sleep(3);

    for (int i = 0; i < num_of_workers; i++)
    {
        if (worker_results[i].pid > 0)
        {
            if (kill(worker_results[i].pid, 0) == 0)
                kill(worker_results[i].pid, SIGKILL);
        }
    }

    for (int i = 0; i < num_of_workers; i++)
    {
        if (worker_results[i].pid <= 0) continue;
        int status;
        waitpid(worker_results[i].pid, &status, 0);
    }
}