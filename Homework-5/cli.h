#ifndef CLI_H
#define CLI_H

typedef struct program_options
{
    int num_couriers;
    const char *input_path;
    const char *stats_path;
} program_options_t;

int parse_program_options(int argc, char **argv, program_options_t *options);
void print_usage(const char *program_name);

#endif
