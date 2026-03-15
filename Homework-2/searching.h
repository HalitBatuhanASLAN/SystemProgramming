#ifndef SEARCHING_H
#define SEARCHING_H

#include "pattern_matching.h"

typedef struct
{
    int match_count;
    int scan_count;
}Searching_Result;

void search_directory(const char *directory_path, const char *pattern, long min_size, Searching_Result *result);

void init_searching_result(Searching_Result *result);

#endif