#ifndef SERVER_OPTIONS_H
#define SERVER_OPTIONS_H

#include "server_types.h"

int parse_server_options(int argc, char **argv, server_options_t *options);
void print_server_usage(void);

#endif
