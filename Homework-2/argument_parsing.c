#include "argument_parsing.h"

#include <stdio.h>   /* fprintf() — printing error and usage messages  */
#include <stdlib.h>  /* exit(), atoi(), atol() — exit and conversions   */
#include <unistd.h>  /* getopt(), optarg — command-line argument parser */

void parse_arguments(int argc, char *argv[], ProcSearchArguments *arguments)
{
    arguments->root_directory = NULL;
    arguments->num_of_workers = 0;
    arguments->pattern = NULL;
    arguments->min_size = 0;

    int opt;
    while((opt = getopt(argc, argv, "d:n:f:s:")) != -1)
    {
        switch(opt)
        {
        case 'd':
            arguments->root_directory = optarg;
            break;
        case 'n':
            arguments->num_of_workers = atoi(optarg);
            break;
        case 'f':
            arguments->pattern = optarg;
            break;
        case 's':
            arguments->min_size = atol(optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s -d <root_directory> -n <num_of_workers> -f <pattern> -s <min_size>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if(arguments->root_directory == NULL ||
        arguments->num_of_workers == 0    ||
        arguments->pattern        == NULL)
    {
        fprintf(stderr, "Usage: %s -d <root_directory> -n <num_of_workers> -f <pattern> -s <min_size>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if(arguments->num_of_workers < 2 || arguments->num_of_workers > 8)
    {
        fprintf(stderr, "Error: num_workers must be between 2 and 8.\n");
        exit(EXIT_FAILURE);
    }

}