#define _POSIX_C_SOURCE 200809L

#include "server.h"

#include "common.h"
#include "ingredients.h"
#include "server_clients.h"
#include "server_log.h"
#include "server_options.h"
#include "server_protocol.h"
#include "server_types.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static volatile sig_atomic_t server_stop_requested = 0;

static void server_sigint_handler(int signal_number)
{
    (void) signal_number;
    // Async safe way: only set a flag and let main loop notice it.
    server_stop_requested = 1;
}

static void disconnect_idle_clients(server_context_t *context, int timeout_value)
{
    time_t now;
    int i;

    // Timeout is checked before each select call, without sleep.
    now = time(NULL);
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0)
        {
            long elapsed;

            elapsed = (long) difftime(now, context->clients[i].last_active);
            if (elapsed >= timeout_value)
            {
                send_line(context->clients[i].fd, "TIMEOUT DISCONNECT");
                server_log_event(context, "TIMEOUT username=%s fd=%d elapsed=%lds", context->clients[i].username[0] == '\0' ? "UNKNOWN" : context->clients[i].username, context->clients[i].fd, elapsed);
                close_client(context, i, "timeout");
            }
        }
    }
}

static void build_select_set(server_context_t *context, fd_set *read_fds, int *max_fd)
{
    int i;

    // Listen socket and all active clients goes into same fd_set.
    FD_ZERO(read_fds);
    FD_SET(context->listen_fd, read_fds);
    *max_fd = context->listen_fd;
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0)
        {
            FD_SET(context->clients[i].fd, read_fds);
            if (context->clients[i].fd > *max_fd)
            {
                *max_fd = context->clients[i].fd;
            }
        }
    }
}

static int calculate_timeout(server_context_t *context, int timeout_value, struct timeval *select_timeout)
{
    time_t now;
    int i;
    long min_remaining;
    int has_client;

    // select waits only until the nearest client can timeout.
    now = time(NULL);
    min_remaining = timeout_value;
    has_client = 0;
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0)
        {
            long elapsed;
            long remaining;

            elapsed = (long) difftime(now, context->clients[i].last_active);
            remaining = timeout_value - elapsed;
            if (remaining < 0)
            {
                remaining = 0;
            }
            if (!has_client || remaining < min_remaining)
            {
                min_remaining = remaining;
            }
            has_client = 1;
        }
    }
    if (!has_client)
    {
        return 0;
    }
    select_timeout->tv_sec = min_remaining;
    select_timeout->tv_usec = 0;
    return 1;
}

static void shutdown_server(server_context_t *context)
{
    server_log_event(context, "SHUTDOWN signal=SIGINT");
    shutdown_connected_clients(context);
    if (context->listen_fd >= 0)
    {
        close(context->listen_fd);
        context->listen_fd = -1;
    }
    server_log_event(context, "CLEANUP_DONE clients=0");
}

static void run_server_loop(server_context_t *context, int timeout_value)
{
    while (!server_stop_requested)
    {
        fd_set read_fds;
        int max_fd;
        int ready_count;
        struct timeval select_timeout;
        struct timeval *timeout_ptr;
        int i;

        // This is the single threaded event loo.
        disconnect_idle_clients(context, timeout_value);
        timeout_ptr = calculate_timeout(context, timeout_value, &select_timeout) ? &select_timeout : NULL;
        build_select_set(context, &read_fds, &max_fd);
        ready_count = select(max_fd + 1, &read_fds, NULL, NULL, timeout_ptr);
        if (ready_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (ready_count == 0)
        {
            continue;
        }
        if (FD_ISSET(context->listen_fd, &read_fds))
        {
            accept_new_client(context);
        }
        for (i = 0; i < context->max_clients; i++)
        {
            if (context->clients[i].fd >= 0 && FD_ISSET(context->clients[i].fd, &read_fds))
            {
                process_client_bytes(context, i);
            }
        }
    }
}

static int prepare_context(server_context_t *context, const server_options_t *options)
{
    // Prepare every resource before entering server loop.
    memset(context, 0, sizeof(*context));
    context->listen_fd = -1;
    ingredient_table_init(&context->ingredients);
    if (ingredient_table_load(&context->ingredients, options->ingredients_path) < 0)
    {
        return -1;
    }
    context->log_file = fopen(options->log_path, "w");
    if (context->log_file == NULL)
    {
        ingredient_table_free(&context->ingredients);
        return -1;
    }
    context->clients = calloc((size_t) options->max_clients, sizeof(client_t));
    if (context->clients == NULL)
    {
        fclose(context->log_file);
        ingredient_table_free(&context->ingredients);
        return -1;
    }
    context->max_clients = options->max_clients;
    init_client_slots(context);
    context->listen_fd = open_server_socket(options->tcp_port);
    if (context->listen_fd < 0)
    {
        free(context->clients);
        fclose(context->log_file);
        ingredient_table_free(&context->ingredients);
        return -1;
    }
    return 0;
}

static void free_context(server_context_t *context)
{
    free(context->clients);
    if (context->log_file != NULL)
    {
        fclose(context->log_file);
    }
    ingredient_table_free(&context->ingredients);
}

int run_hogwarts_server(int argc, char **argv)
{
    server_options_t options;
    server_context_t context;
    struct sigaction action;

    signal(SIGPIPE, SIG_IGN);
    if (parse_server_options(argc, argv, &options) < 0)
    {
        print_server_usage();
        return 1;
    }
    if (prepare_context(&context, &options) < 0)
    {
        print_server_usage();
        return 1;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = server_sigint_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    server_log_event(&context, "SERVER_STARTED port=%d max_clients=%d timeout=%d ingredients=%zu", options.tcp_port, options.max_clients, options.timeout, context.ingredients.count);
    printf("Hogwarts is ready. Port: %d | Max Clients: %d | Timeout: %ds\n", options.tcp_port, options.max_clients, options.timeout);
    fflush(stdout);
    run_server_loop(&context, options.timeout);
    shutdown_server(&context);
    free_context(&context);
    return 0;
}
