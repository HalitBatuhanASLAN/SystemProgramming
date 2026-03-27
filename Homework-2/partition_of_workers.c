#include"partition_of_workers.h"

#include <stdio.h>/* fprintf()*/
#include <stdlib.h>/* malloc(), free(), exit()*/
#include <string.h>/*strdup(), snprintf()*/
#include <dirent.h>/*opendir(), readdir(), closedir()*/
#include <sys/stat.h>/*lstat(), S_ISDIR()*/

int partition_directories(const char *root_directory, int num_of_workers, Worker_Partition partitions[MAX_WORKERS])
{
    /*
        initialize partitions array to null and 0 for avoiding dangling pointers
    */
    for(int i = 0; i<num_of_workers;i++)
    {
        partitions[i].num_of_subdirectories = 0;
        for(int j = 0; j<MAX_SUBDIRECTORIES;j++)
            partitions[i].directories[j] = NULL;
    }

    /*opens current directory and cghecks if null or not*/
    DIR *directory = opendir(root_directory);
    if(directory == NULL)
    {
        fprintf(stderr, "Error: cannot open directory '%s'\n", root_directory);
        return 0;
    }

    char *subdirectories[MAX_SUBDIRECTORIES];
    int   subdir_count = 0;

    /*to take directory entries*/
    struct dirent *dp;
    struct stat st;
    char full_path[MAX_PATH_LENGTH];

    while((dp = readdir(directory)) != NULL)
    {
        /*skip . and .. links*/
        if(strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        snprintf(full_path, MAX_PATH_LENGTH, "%s/%s", root_directory, dp->d_name);
        
        /*by using lstat take stat info*/
        if(lstat(full_path, &st) == -1)
            continue;
        /*check with that Macro if really a directory*/
        if(!S_ISDIR(st.st_mode))
            continue;

        if(subdir_count >= MAX_SUBDIRECTORIES)
        {
            fprintf(stderr, "Warning: too many subdirectories, "
                            "truncating at %d\n", MAX_SUBDIRECTORIES);
            break;
        }
        /*by using strdup(string duplicate) put file path into heap */
        subdirectories[subdir_count] = strdup(full_path);
        if(subdirectories[subdir_count] == NULL)
        {
            fprintf(stderr, "Error: strdup failed\n");
            closedir(directory);
            return 0;
        }
        subdir_count++;
    }
    closedir(directory);

    if(subdir_count == 0)
    {
        printf("Notice: no subdirectories found; parent will search root directly.\n");
        return 0;
    }

    if(num_of_workers > subdir_count)
    {
        printf("Notice: only %d subdirectories found; using %d workers instead of %d.\n",
                subdir_count, subdir_count, num_of_workers);
        num_of_workers = subdir_count;
    }

    /*Round-robin assign subdirectories into workers */
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
    /* through all all workers, free heap mem allocated by strdup*/
    for(int i = 0; i<num_of_workers;i++)
    {
        for(int j = 0; j<partitions[i].num_of_subdirectories;j++)
        {
            free(partitions[i].directories[j]);
            partitions[i].directories[j] = NULL;/*just to prevent dangling pointer*/
        }
        partitions[i].num_of_subdirectories = 0;
    }
}