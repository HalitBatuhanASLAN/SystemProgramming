#ifndef WORKER_H
#define WORKER_H

#include "partition_of_workers.h" /* Worker_Partition  */
#include "signals_handler.h"      /* got_sigterm       */
#include "searching.h"               /* search_directory  */
#include "print_result.h"         /* Worker_Result     */

void launch_workers(Worker_Partition partitions[MAX_WORKERS], int num_of_workers, const char *pattern, long min_size, Worker_Result worker_results[MAX_WORKERS]);

void wait_for_workers(const int num_of_workers, Worker_Result worker_results[MAX_WORKERS]);

void kill_workers(const int num_of_workers, Worker_Result worker_results[MAX_WORKERS]);

#endif