#define _POSIX_C_SOURCE 200809L

#include "server_clients.h"

#include "common.h"
#include "server_log.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int open_server_socket(int tcp_port)
{
    int fd;
    int enabled;
    struct sockaddr_in address;

    // One listening TCP socket is used for both wizards and professors.
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    enabled = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t) tcp_port);
    if (bind(fd, (struct sockaddr *) &address, sizeof(address)) < 0)
    {
        close(fd);
        return -1;
    }
    if (listen(fd, 64) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

void init_client_slots(server_context_t *context)
{
    int i;

    for (i = 0; i < context->max_clients; i++)
    {
        context->clients[i].fd = -1;
        context->clients[i].spellbook = NULL;
    }
}

int username_exists(server_context_t *context, const char *username)
{
    int i;

    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0 && context->clients[i].username[0] != '\0' && strcmp(context->clients[i].username, username) == 0)
        {
            return 1;
        }
    }
    return 0;
}

int add_client(server_context_t *context, int fd)
{
    int i;

    // New client gets its own line buffer and spellbook area.
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd < 0)
        {
            context->clients[i].fd = fd;
            context->clients[i].username[0] = '\0';
            context->clients[i].type[0] = '\0';
            context->clients[i].line_len = 0;
            context->clients[i].line_too_long = 0;
            context->clients[i].last_active = time(NULL);
            context->clients[i].spellbook = calloc(context->ingredients.count == 0 ? 1 : context->ingredients.count, sizeof(long));
            if (context->clients[i].spellbook == NULL)
            {
                close(fd);
                context->clients[i].fd = -1;
                return -1;
            }
            context->client_count++;
            return i;
        }
    }
    return -1;
}

void close_client(server_context_t *context, int index, const char *reason)
{
    char username_text[MAX_USERNAME_LENGTH + 1];

    // Closing also frees spellbook, because it is not persistent.
    if (context->clients[index].fd < 0)
    {
        return;
    }
    snprintf(username_text, sizeof(username_text), "%s", context->clients[index].username[0] == '\0' ? "UNKNOWN" : context->clients[index].username);
    close(context->clients[index].fd);
    free(context->clients[index].spellbook);
    context->clients[index].fd = -1;
    context->clients[index].username[0] = '\0';
    context->clients[index].type[0] = '\0';
    context->clients[index].line_len = 0;
    context->clients[index].line_too_long = 0;
    context->clients[index].last_active = 0;
    context->clients[index].spellbook = NULL;
    context->client_count--;
    server_log_event(context, "CLIENT_DISCONNECTED username=%s reason=%s", username_text, reason);
}

void accept_new_client(server_context_t *context)
{
    struct sockaddr_in peer_address;
    socklen_t peer_length;
    int fd;
    char ip_text[INET_ADDRSTRLEN];

    // Even when full, server accepts first then sends clear error.
    peer_length = sizeof(peer_address);
    fd = accept(context->listen_fd, (struct sockaddr *) &peer_address, &peer_length);
    if (fd < 0)
    {
        return;
    }
    if (inet_ntop(AF_INET, &peer_address.sin_addr, ip_text, sizeof(ip_text)) == NULL)
    {
        snprintf(ip_text, sizeof(ip_text), "unknown");
    }
    if (context->client_count >= context->max_clients)
    {
        send_line(fd, "ERR HOGWARTS_FULL");
        server_log_event(context, "REJECTED fd=%d ip=%s reason=HOGWARTS_FULL", fd, ip_text);
        close(fd);
        return;
    }
    if (add_client(context, fd) < 0)
    {
        send_line(fd, "ERR HOGWARTS_FULL");
        server_log_event(context, "REJECTED fd=%d ip=%s reason=HOGWARTS_FULL", fd, ip_text);
        close(fd);
        return;
    }
    server_log_event(context, "CLIENT_CONNECTED fd=%d ip=%s", fd, ip_text);
}

void shutdown_connected_clients(server_context_t *context)
{
    int i;

    // On SIGINT all connected clients get shutdown notice.
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0)
        {
            send_line(context->clients[i].fd, "SERVER SHUTDOWN");
            close(context->clients[i].fd);
            free(context->clients[i].spellbook);
            context->clients[i].fd = -1;
            context->clients[i].spellbook = NULL;
            context->clients[i].username[0] = '\0';
            context->clients[i].type[0] = '\0';
        }
    }
    context->client_count = 0;
}
