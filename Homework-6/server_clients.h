#ifndef SERVER_CLIENTS_H
#define SERVER_CLIENTS_H

#include "server_types.h"

int open_server_socket(int tcp_port);
void init_client_slots(server_context_t *context);
int username_exists(server_context_t *context, const char *username);
int add_client(server_context_t *context, int fd);
void close_client(server_context_t *context, int index, const char *reason);
void accept_new_client(server_context_t *context);
void shutdown_connected_clients(server_context_t *context);

#endif
