#include "argument_parsing.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void parse_arguments(int argc, char *argv[], ProcSearchArguments *arguments)
{
    /*initialize struct features*/
    arguments->root_directory = NULL;
    arguments->num_of_workers = 0;
    arguments->pattern = NULL;
    arguments->min_size = 0;

    int opt;
    /*by using getopt parse command line, : means option requires an argument*/
    while((opt = getopt(argc, argv, "d:n:f:s:")) != -1)
    {
        switch(opt)
        {
        case 'd':/*directory path*/
            arguments->root_directory = optarg;
            break;
        case 'n':/*norker nubmer*/
            arguments->num_of_workers = atoi(optarg);
            break;
        case 'f':/*file pattern*/
            arguments->pattern = optarg;
            break;
        case 's':/*take min file size*/
            arguments->min_size = atol(optarg);
            break;
        default:/*give correct usagge*/
            fprintf(stderr, "Usage: %s -d <root_dir> -n <num_workers> -f <pattern> [-s <min_size_bytes>]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    /*check if one is missing or not*/
    if(arguments->root_directory == NULL ||
        arguments->num_of_workers == 0    ||
        arguments->pattern        == NULL)
    {
        fprintf(stderr, "Usage: %s -d <root_dir> -n <num_workers> -f <pattern> [-s <min_size_bytes>]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /*check for worker bounds*/
    if(arguments->num_of_workers < 2 || arguments->num_of_workers > 8)
    {
        fprintf(stderr, "Error: num_workers must be between 2 and 8.\n");
        exit(EXIT_FAILURE);
    }

    /*clean 2 slashes to prevent later path conflicts*/
    size_t len = strlen(arguments->root_directory);
    if (len > 1 && arguments->root_directory[len - 1] == '/') {
        arguments->root_directory[len - 1] = '\0';
    }
}