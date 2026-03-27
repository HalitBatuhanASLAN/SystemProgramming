#ifndef WORKER_H
#define WORKER_H

#include "partition_of_workers.h"
#include "signals_handler.h"
#include "searching.h"
#include "print_result.h"

/* spawn child processes using fork and initialize their search works*/
void launch_workers(Worker_Partition partitions[MAX_WORKERS], int num_of_workers, const char *pattern, long min_size, Worker_Result worker_results[MAX_WORKERS]);

/*till all workers completed suspends parent process*/
void wait_for_workers(const int num_of_workers, Worker_Result worker_results[MAX_WORKERS]);

/*kills(terminates) workers(child processes)*/
void kill_workers(const int num_of_workers, Worker_Result worker_results[MAX_WORKERS]);

#endif