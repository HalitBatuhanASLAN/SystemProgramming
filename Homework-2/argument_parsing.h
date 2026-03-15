#ifndef ARGUMENT_PARSING_H
#define ARGUMENT_PARSING_H

typedef struct
{
    char *root_directory;
    int num_of_workers;
    char *pattern;
    long min_size;
} ProcSearchArguments;

void parse_arguments(int argc, char *argv[], ProcSearchArguments *arguments);

#endif