#ifndef PARTITION_OF_WORKERS_H
#define PARTITION_OF_WORKERS_H

/*limits for child processes*/
#define MAX_WORKERS 8
#define MIN_WORKERS 2

/*max subdirectory number a process can handle(search) and max path lenght0*/
#define MAX_SUBDIRECTORIES 512
#define MAX_PATH_LENGTH 4096

/*holds directories list which assigned a specific worker*/
typedef struct
{
    char *directories[MAX_SUBDIRECTORIES];
    int num_of_subdirectories;
}Worker_Partition;

/*
    it scans root directoyry and assign subdirectories into workers for aearch pattern
*/
int partition_directories(const char *root_directory, int num_of_workers, Worker_Partition partitions[MAX_WORKERS]);

/*
    free dynamic allocated mem spaces for directory strings
*/
void free_partitions(Worker_Partition partitions[MAX_WORKERS], int num_of_workers);

#endif