#ifndef SERVER_LOG_H
#define SERVER_LOG_H

#include "server_types.h"

void server_log_event(server_context_t *context, const char *format, ...);

#endif
