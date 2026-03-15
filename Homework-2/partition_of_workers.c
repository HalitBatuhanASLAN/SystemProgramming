#include"partiton_of_workers.h"

#include <stdio.h>    /* fprintf()                          */
#include <stdlib.h>   /* malloc(), free(), exit()           */
#include <string.h>   /* strdup(), snprintf()               */
#include <dirent.h>   /* opendir(), readdir(), closedir()   */
#include <sys/stat.h> /* lstat(), S_ISDIR()                 */

int partiton_of_workers(const char *root_directory, int num_of_workers, Worker_Partition partitions[MAX_WORKERS])
{
    for(int i = 0; i<num_of_workers;i++)
    {
        partitions[i].num_of_subdirectories = 0;
        for(int j = 0; j<MAX_SUBDIRECTORIES;j++)
            partitions[i].directories[j] = NULL;
    }

    DIR *directory = opendir(root_directory);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", root_directory);
        return 0;
    }

    char *subdirectories[MAX_SUBDIRECTORIES];
    int   subdir_count = 0;

    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];

    while((dp = readdir(directory)) != NULL)
    {
        if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", root_directory, dp->d_name);
        
        if(lstat(full_path, &st) == -1)
            continue;
        if(!S_ISDIR(st.st_mode))
            continue;

        if(subdir_count >= MAX_SUBDIRECTORIES)
        {
            fprintf(stderr, "Warning: too many subdirectories, "
                            "truncating at %d\n", MAX_SUBDIRECTORIES);
            break;
        }
        subdirectories[subdir_count] = strdup(full_path);
        if(subdirectories[subdir_count] == NULL)
        {
            fprintf(stderr, "Error: strdup failed\n");
            closedir(dirp);
            return 0;
        }
        subdir_count++;
    }
    closedir(directory);

    if(subdir_count == 0)
    {
        fprintf(stderr, "Notice: no subdirectories found; parent will search root directly.\n");
        return 0;
    }

    if(num_of_workers > subdir_count)
    {
        fprintf(stderr, "Notice: only %d subdirectories found; using %d workers instead of %d.\n",
                subdir_count, subdir_count, num_of_workers);
        num_of_workers = subdir_count;
    }

    /* Round-robin assignment of subdirectories to workers */
    for(int i = 0; i<subdir_count;i++)
    {
        int worker_id = i % num_of_workers;
        Worker_Partition *worker_partition = &partitions[worker_id];
        worker_partition->directories[worker_partition->num_of_subdirectories] = subdirectories[i];
        worker_partition->num_of_subdirectories++;
    }
    return num_of_workers;
}

void free_partitions(Worker_Partition partitions[MAX_WORKERS], int num_of_workers)
{
    for(int i = 0; i<num_of_workers;i++)
    {
        for(int j = 0; j<partitions[i].num_of_subdirectories;j++)
        {
            free(partitions[i].directories[j]);
            partitions[i].directories[j] = NULL;
        }
        partitions[i].num_of_subdirectories = 0;
    }
}