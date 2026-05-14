#define _POSIX_C_SOURCE 200809L

#include "server_options.h"

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

void print_server_usage(void)
{
    fprintf(stderr, "Usage: ./hogwarts -p <tcp_port> -s <ingredients.txt> -l <logfile> -n <max_clients> -t <timeout>\n");
}

int parse_server_options(int argc, char **argv, server_options_t *options)
{
    int flag;
    int has_port;
    int has_source;
    int has_log;
    int has_max;
    int has_timeout;

    memset(options, 0, sizeof(*options));
    has_port = 0;
    has_source = 0;
    has_log = 0;
    has_max = 0;
    has_timeout = 0;
    while ((flag = getopt(argc, argv, "p:s:l:n:t:")) != -1)
    {
        if (flag == 'p')
        {
            if (parse_int_value(optarg, 1024, 65535, &options->tcp_port) < 0)
            {
                return -1;
            }
            has_port = 1;
        }
        else if (flag == 's')
        {
            snprintf(options->ingredients_path, sizeof(options->ingredients_path), "%s", optarg);
            has_source = 1;
        }
        else if (flag == 'l')
        {
            snprintf(options->log_path, sizeof(options->log_path), "%s", optarg);
            has_log = 1;
        }
        else if (flag == 'n')
        {
            if (parse_int_value(optarg, 1, 100000, &options->max_clients) < 0)
            {
                return -1;
            }
            has_max = 1;
        }
        else if (flag == 't')
        {
            if (parse_int_value(optarg, 1, 100000, &options->timeout) < 0)
            {
                return -1;
            }
            has_timeout = 1;
        }
        else
        {
            return -1;
        }
    }
    if (!has_port || !has_source || !has_log || !has_max || !has_timeout || optind != argc)
    {
        return -1;
    }
    return 0;
}
