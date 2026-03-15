#ifndef PARTITION_OF_WORKERS_H
#define PARTITION_OF_WORKERS_H

#define MAX_WORKERS 8
#define MIN_WORKERS 2
#define MAX_SUBDIRECTORIES 512

#define MAX_PATH_LENGTH 4096

typedef struct
{
    char *directories[MAX_SUBDIRECTORIES];
    int num_of_subdirectories;
}Worker_Partition;

int partition_directories(const char *root_directory, int num_of_workers, Worker_Partition partitions[MAX_WORKERS]);

void free_partitions(Worker_Partition partitions[MAX_WORKERS], int num_of_workers);

#endif