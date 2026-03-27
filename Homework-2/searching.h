#ifndef SEARCHING_H
#define SEARCHING_H

#include "pattern_matching.h"

#include<stdio.h>

/* struct to keep statistic results*/
typedef struct
{
    int match_count;
    int scan_count;
} Searching_Result;

/*recursivly search a directory */
void search_directory(const char *directory_path, const char *pattern,long min_size, Searching_Result *result, int verbose, FILE *worker_file);

/*main process checks files in root directory by itself*/
void search_root_files(const char *directory_path, const char *pattern,long min_size, Searching_Result *result, FILE *worker_file);

/*init results counts to zero*/
void init_searching_result(Searching_Result *result);

#endif