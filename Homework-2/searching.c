#include "searching.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "signals_handler.h"  /* got_sigterm */

#define MAX_PATH_LENGTH 4096

/* just initialize starting statictics to 0*/
void init_searching_result(Searching_Result *result)
{
    result->match_count = 0;
    result->scan_count  = 0;
}

/**/
void search_directory(const char *directory_path, const char *pattern,long min_size, Searching_Result *result, int verbose, FILE *worker_file)
{
    DIR *directory;
    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];

    /*check for parent process send a SIGTERM*/
    if(got_sigterm) return;

    directory = opendir(directory_path);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", directory_path);
        return;
    }

    /*Standard loop pattern*/
    for(;;)
    {
        /*check for sigterm*/
        if (got_sigterm) break;

        errno = 0;
        dp = readdir(directory);
        if(dp == NULL)break;

        if(strcmp(dp->d_name, ".")  == 0) continue;
        if(strcmp(dp->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path),
                 "%s/%s", directory_path, dp->d_name);

        if(lstat(full_path, &st) == -1)
        {
            fprintf(stderr, "Error: lstat failed for '%s'\n", full_path);
            continue;
        }

        /*if another directory then recursibly check for that too*/
        if(S_ISDIR(st.st_mode))
            search_directory(full_path, pattern, min_size, result, verbose, worker_file);

        else if(S_ISREG(st.st_mode))
        {
            result->scan_count++;

            if(!is_match_pattern(dp->d_name, pattern))
                continue;

            if(min_size > 0 && st.st_size < min_size)
                continue;

            result->match_count++;

            /*
                if verbose is 1 then directly write output to consoe, if 0 then just write intto a file is enough
            */
            if(verbose)
                printf("[Worker PID:%d] MATCH: %s (%lld bytes)\n",
                       (int)getpid(),
                       full_path,
                       (long long)st.st_size);

            /*write into a file*/
            if(worker_file != NULL)
                fprintf(worker_file, "%d %s (%lld bytes)\n",
                        (int)getpid(), full_path, (long long)st.st_size);
        }
    }

    if(errno != 0)
        fprintf(stderr, "Error: readdir failed for '%s'\n", directory_path);

    closedir(directory);
}

/*
    just use main process to chek fleis in directly main directory
*/
void search_root_files(const char *directory_path, const char *pattern,long min_size, Searching_Result *result, FILE *worker_file)
{
    DIR *directory;
    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];

    directory = opendir(directory_path);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", directory_path);
        return;
    }

    for(;;)
    {
        errno = 0;
        dp = readdir(directory);
        if (dp == NULL) break;

        if (strcmp(dp->d_name, ".")  == 0) continue;
        if (strcmp(dp->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path),
                 "%s/%s", directory_path, dp->d_name);

        if(lstat(full_path, &st) == -1) continue;

        if(!S_ISREG(st.st_mode)) continue;

        result->scan_count++;

        if(!is_match_pattern(dp->d_name, pattern))
            continue;

        if(min_size > 0 && st.st_size < min_size)
            continue;

        result->match_count++;

        printf("[Parent PID:%d] MATCH: %s (%lld bytes)\n",
               (int)getpid(), full_path, (long long)st.st_size);

        if(worker_file != NULL)
            fprintf(worker_file, "%d %s (%lld bytes)\n",(int)getpid(), full_path, (long long)st.st_size);
    }

    if(errno != 0)
        fprintf(stderr, "Error: readdir failed for '%s'\n", directory_path);

    closedir(directory);
}