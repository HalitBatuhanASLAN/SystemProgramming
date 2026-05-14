#define _POSIX_C_SOURCE 200809L

#include "server_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

void server_log_event(server_context_t *context, const char *format, ...)
{
    time_t now;
    struct tm time_info;
    char time_buf[32];
    char message[1024];
    va_list args;

    now = time(NULL);
    localtime_r(&now, &time_info);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &time_info);
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    printf("[%s] [SERVER] %s\n", time_buf, message);
    fflush(stdout);
    if (context->log_file != NULL)
    {
        fprintf(context->log_file, "[%s] [SERVER] %s\n", time_buf, message);
        fflush(context->log_file);
    }
}
