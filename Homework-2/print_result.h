#ifndef PRINT_RESULT_H
#define PRINT_RESULT_H

#include "partition_of_workers.h" /* Worker_Partition */
#include "searching.h" /* Search_Result */

/* to keep process id and succesfull mathes count*/
typedef struct
{
    int pid;
    int match_count;
} Worker_Result;

/*print detail tree format*/
void print_tree_with_scanned(const char *root_directory, Worker_Result *worker_results, int num_of_workers, int *total_scanned_out);

/*just prints summary part*/
void print_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS]);

/*prints summary for partial part(sigint situation)*/
void print_partial_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS]);

#endif