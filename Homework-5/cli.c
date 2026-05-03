#include "cli.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if(errno != 0 || end == text || *end != '\0' || parsed < 1 || parsed > 2147483647L)
    {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s -n <num_couriers> -i <orders.txt> -s <stats.txt>\n", program_name);
}

int parse_program_options(int argc, char **argv, program_options_t *options)
{
    int index;
    int has_num_couriers = 0;
    int has_input_path = 0;
    int has_stats_path = 0;

    options->num_couriers = 0;
    options->input_path = NULL;
    options->stats_path = NULL;

    for(index = 1; index < argc; index++)
    {
        if(strcmp(argv[index], "-n") == 0 && index + 1 < argc)
        {
            if(!parse_positive_int(argv[index + 1], &options->num_couriers))
            {
                return 0;
            }

            has_num_couriers = 1;
            index++;
        }
        else if(strcmp(argv[index], "-i") == 0 && index + 1 < argc)
        {
            options->input_path = argv[index + 1];
            has_input_path = 1;
            index++;
        }
        else if(strcmp(argv[index], "-s") == 0 && index + 1 < argc)
        {
            options->stats_path = argv[index + 1];
            has_stats_path = 1;
            index++;
        }
        else
        {
            return 0;
        }
    }

    return has_num_couriers && has_input_path && has_stats_path;
}
