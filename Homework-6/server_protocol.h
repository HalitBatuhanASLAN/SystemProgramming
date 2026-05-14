#ifndef SERVER_PROTOCOL_H
#define SERVER_PROTOCOL_H

#include "server_types.h"

int process_client_bytes(server_context_t *context, int client_index);

#endif
