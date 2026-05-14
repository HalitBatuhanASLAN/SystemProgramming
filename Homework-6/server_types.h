#ifndef SERVER_TYPES_H
#define SERVER_TYPES_H

#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include "common.h"
#include "ingredients.h"

typedef struct
{
    int fd;
    char username[MAX_USERNAME_LENGTH + 1];
    char type[MAX_TYPE_LENGTH + 1];
    char line_buf[MAX_LINE_LENGTH];
    size_t line_len;
    int line_too_long;
    time_t last_active;
    long *spellbook;
} client_t;

typedef struct
{
    int tcp_port;
    char ingredients_path[256];
    char log_path[256];
    int max_clients;
    int timeout;
} server_options_t;

typedef struct
{
    FILE *log_file;
    ingredient_table_t ingredients;
    client_t *clients;
    int max_clients;
    int client_count;
    int listen_fd;
} server_context_t;

#endif
