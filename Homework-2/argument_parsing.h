#ifndef ARGUMENT_PARSING_H
#define ARGUMENT_PARSING_H

/*
    struct-> to keep comman line arguments for program 
*/
typedef struct
{
    char *root_directory;/* path of target directory*/
    int num_of_workers;/* worker(child process) number*/
    char *pattern;/*regelike pattern for criterias*/
    long min_size;/*optional min file size for search*/
} ProcSearchArguments;

/*
    parsing command line arguments and stores in a struct, uses getopt
*/
void parse_arguments(int argc, char *argv[], ProcSearchArguments *arguments);

#endif