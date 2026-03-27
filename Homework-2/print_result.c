#include "print_result.h"
#include "pattern_matching.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 4096
#define MAX_MATCHES 8192

/*represent a signle mathed file read from temporary file*/
typedef struct
{
    char path[MAX_PATH_LENGTH];
    char display[MAX_PATH_LENGTH];
    int  worker_pid;
} MatchEntry;

static MatchEntry all_matches[MAX_MATCHES];
static int all_matches_count = 0;

/*chekcs if a spesific path is in the matches array*/
static int path_in_matches(const char *path)
{
    for(int i = 0; i < all_matches_count; i++)
    {
        if(strcmp(all_matches[i].path, path) == 0)
            return i;
    }
    return -1;
}

/*checks if a directory contins ant matches*/
static int has_match_in_directory(const char *dir_path)
{
    size_t dir_path_len = strlen(dir_path);
    for(int i = 0; i < all_matches_count; i++)
    {
        if (strncmp(all_matches[i].path, dir_path, dir_path_len) == 0
            && all_matches[i].path[dir_path_len] == '/')
            return 1;
    }
    return 0;
}

/*printing tree format according to teacher answer*/
static void print_indents(int level)
{
    printf("|");
    int dash_num = 2 + (level - 1) * 6;
    for (int i = 0; i < dash_num; i++)
        printf("-");
}

/*recursivly travels tree directory tree and prints matches */
static int print_tree_recursive(const char  *dir_path,int depth)
{
    DIR *dirp;
    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];
    int found = 0;

    dirp = opendir(dir_path);
    if(dirp == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", dir_path);
        return 0;
    }

    for(;;)
    {
        errno = 0;
        dp = readdir(dirp);
        if (dp == NULL) break;

        if (strcmp(dp->d_name, ".")  == 0) continue;
        if (strcmp(dp->d_name, "..") == 0) continue;

        snprintf(full_path, sizeof(full_path),
                 "%s/%s", dir_path, dp->d_name);

        if(lstat(full_path, &st) == -1) continue;

        if(S_ISDIR(st.st_mode))
        {
            if(!has_match_in_directory(full_path))
                continue;
            print_indents(depth);
            printf(" %s\n", dp->d_name);
            found += print_tree_recursive(full_path, depth + 1);
        }
        else if(S_ISREG(st.st_mode))
        {
            /*check if file recorded as matched*/
            int idx = path_in_matches(full_path);
            if(idx < 0) continue;

            print_indents(depth);
            /*take file informations and print*/
            const char *size_part = all_matches[idx].display;
            const char *paren = strstr(size_part, " (");
            if(paren)
                printf(" %s%s [Worker %d]\n", dp->d_name, paren, all_matches[idx].worker_pid);
            else
                printf(" %s [Worker %d]\n", dp->d_name, all_matches[idx].worker_pid);
            found++;
        }
    }

    if (errno != 0)
        fprintf(stderr, "Error: readdir failed for '%s'\n", dir_path);

    closedir(dirp);
    return found;
}

/*
    for the ones who created by worker processes
*/
static int load_worker_files(Worker_Result *worker_results, int num_of_workers,int *total_scanned)
{
    all_matches_count = 0;
    *total_scanned = 0;
    for (int i = 0; i < num_of_workers; i++) {
        char filename[64];
        snprintf(filename, sizeof(filename),
                 "/tmp/worker_%d.txt", worker_results[i].pid);

        FILE *f = fopen(filename, "r");
        if (f == NULL) continue;

        char line[MAX_PATH_LENGTH];
        int first_line = 1;
        int file_match_count = 0;

        while(fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\n")] = '\0';
            if(strlen(line) == 0) continue;

            if(first_line)
            {
                /* "Scanned:42" format*/
                int scanned = 0;
                if(sscanf(line, "Scanned:%d", &scanned) == 1)
                    *total_scanned += scanned;
                first_line = 0;
                continue;
            }

            file_match_count++;

            /* Match: "PID /path/file.txt (22 bytes)" */
            if(all_matches_count < MAX_MATCHES)
            {
                int pid = 0;
                char rest[MAX_PATH_LENGTH];

                char *space = strchr(line, ' ');
                if (space == NULL) continue;

                *space = '\0';
                pid = atoi(line);
                *space = ' ';

                snprintf(rest, sizeof(rest), "%s", space + 1);

                char path_buf[MAX_PATH_LENGTH];
                const char *paren = strstr(rest, " (");
                if(paren)
                {
                    int plen = (int)(paren - rest);
                    strncpy(path_buf, rest, plen);
                    path_buf[plen] = '\0';
                }
                else
                {
                    strncpy(path_buf, rest, MAX_PATH_LENGTH - 1);
                    path_buf[MAX_PATH_LENGTH - 1] = '\0';
                }

                strncpy(all_matches[all_matches_count].path, path_buf, MAX_PATH_LENGTH - 1);
                all_matches[all_matches_count].path[MAX_PATH_LENGTH - 1] = '\0';

                strncpy(all_matches[all_matches_count].display, rest, MAX_PATH_LENGTH - 1);
                all_matches[all_matches_count].display[MAX_PATH_LENGTH - 1] = '\0';

                all_matches[all_matches_count].worker_pid = pid;
                all_matches_count++;
            }
        }
        fclose(f);
        remove(filename);/*remove file*/

        worker_results[i].match_count = file_match_count;
    }

    return all_matches_count;
}

void print_tree_with_scanned(const char *root_directory, Worker_Result *worker_results, int num_of_workers, int *total_scanned_out)
{
    *total_scanned_out = 0;
    load_worker_files(worker_results, num_of_workers, total_scanned_out);
    printf("%s\n", root_directory);
    if (all_matches_count == 0)
    {
        printf("No matching files found.\n");
        return;
    }
    print_tree_recursive(root_directory, 1);
}

void print_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS])
{
    printf("--- Summary ---\n");
    printf("Total workers used   : %d\n", num_of_workers);
    printf("Total files scanned  : %d\n", total_scanned);
    printf("Total matches found  : %d\n", total_matched);

    for (int i = 0; i < num_of_workers; i++)
    {
        printf("Worker PID %-8d : %d %s\n",
               worker_results[i].pid,
               worker_results[i].match_count,
               worker_results[i].match_count == 1 ? "match" : "matches");
    }
}

void print_partial_summary(int num_of_workers, int total_scanned, int total_matched, Worker_Result worker_results[MAX_WORKERS])
{
    printf("--- Summary (partial) ---\n");
    printf("Total workers used   : %d\n", num_of_workers);
    printf("Total files scanned  : %d\n", total_scanned);
    printf("Total matches found  : %d\n", total_matched);

    for (int i = 0; i < num_of_workers; i++) {
        printf("Worker PID %-8d : %d %s\n",
               worker_results[i].pid,
               worker_results[i].match_count,
               worker_results[i].match_count == 1 ? "match" : "matches");
    }
}