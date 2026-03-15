#ifndef PRINT_RESULT_H
#define PRINT_RESULT_H

#include "partition_of_workers.h"  /* Worker_Partition */
#include "search.h"                /* Search_Result */

typedef struct
{
    int pid;
    int match_count;
} Worker_Result;

void print_tree(const char *root_directory, const char *pattern, long min_size, Worker_Result worker_results[MAX_WORKERS], int num_of_workers);

void print_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS]);

#endif