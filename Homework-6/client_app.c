#define _POSIX_C_SOURCE 200809L

#include "client_app.h"

#include "common.h"

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define CLIENT_RESPONSE_BUFFER 8192

static volatile sig_atomic_t client_stop_requested = 0;

static void client_sigint_handler(int signal_number)
{
    (void) signal_number;
    // Signal handler only sets flag, main loop do the real job.
    client_stop_requested = 1;
}

static void print_client_event(const char *role, const char *username, const char *event, const char *fields)
{
    if (fields != NULL && fields[0] != '\0')
    {
        printf("[%s %s] %s %s\n", role, username, event, fields);
    }
    else
    {
        printf("[%s %s] %s\n", role, username, event);
    }
    fflush(stdout);
}

static int connect_to_server(const char *server_ip, const char *port_text)
{
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *current;
    int socket_fd;

    // getaddrinfo makes connection code cleaner for client side.
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(server_ip, port_text, &hints, &results) != 0)
    {
        return -1;
    }
    socket_fd = -1;
    for (current = results; current != NULL; current = current->ai_next)
    {
        socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (socket_fd < 0)
        {
            continue;
        }
        if (connect(socket_fd, current->ai_addr, current->ai_addrlen) == 0)
        {
            break;
        }
        close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(results);
    return socket_fd;
}

static int process_server_data(const char *role, const char *username, char *line_buf, size_t *line_len, int *disconnect_reason, int socket_fd)
{
    char input_buf[512];
    ssize_t read_count;
    ssize_t i;

    // Server responses may arrive half or many lines together.
    read_count = recv(socket_fd, input_buf, sizeof(input_buf), 0);
    if (read_count < 0)
    {
        if (errno == EINTR)
        {
            return 0;
        }
        *disconnect_reason = 4;
        return 1;
    }
    if (read_count == 0)
    {
        *disconnect_reason = 4;
        return 1;
    }
    for (i = 0; i < read_count; i++)
    {
        char current;

        current = input_buf[i];
        if (current == '\r')
        {
            continue;
        }
        if (current == '\n')
        {
            char fields[CLIENT_RESPONSE_BUFFER + 16];

            line_buf[*line_len] = '\0';
            snprintf(fields, sizeof(fields), "%s", line_buf);
            print_client_event(role, username, "RECEIVED", fields);
            if (strcmp(line_buf, "SERVER SHUTDOWN") == 0)
            {
                *disconnect_reason = 2;
                return 1;
            }
            if (strcmp(line_buf, "TIMEOUT DISCONNECT") == 0)
            {
                *disconnect_reason = 3;
                return 1;
            }
            if (strcmp(line_buf, "OK APPARATE") == 0)
            {
                *disconnect_reason = 1;
                return 1;
            }
            *line_len = 0;
        }
        else if (*line_len + 1 < CLIENT_RESPONSE_BUFFER)
        {
            line_buf[*line_len] = current;
            (*line_len)++;
        }
    }
    return 0;
}

static const char *reason_text(int reason)
{
    if (reason == 1)
    {
        return "APPARATE";
    }
    if (reason == 2)
    {
        return "shutdown";
    }
    if (reason == 3)
    {
        return "timeout";
    }
    return "hangup";
}

static void print_disconnect_event(const char *role, const char *username, int reason)
{
    char fields[64];

    snprintf(fields, sizeof(fields), "reason=%s", reason_text(reason));
    print_client_event(role, username, "DISCONNECTED", fields);
}

int run_client_app(const char *role, const char *server_ip, const char *port_text, const char *username)
{
    int socket_fd;
    char connected_fields[256];
    char enroll_line[128];
    char response_buf[CLIENT_RESPONSE_BUFFER];
    size_t response_len;
    int sent_apparate;
    int disconnect_reason;
    struct sigaction action;

    // Ctrl+C should send APPARATE instead of killing client suddenly.
    signal(SIGPIPE, SIG_IGN);
    memset(&action, 0, sizeof(action));
    action.sa_handler = client_sigint_handler;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    socket_fd = connect_to_server(server_ip, port_text);
    if (socket_fd < 0)
    {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <username>\n", role);
        return 1;
    }
    snprintf(connected_fields, sizeof(connected_fields), "server=%s:%s", server_ip, port_text);
    print_client_event(role, username, "CONNECTED", connected_fields);
    // Every client enrolls automatically after TCP connection.
    snprintf(enroll_line, sizeof(enroll_line), "ENROLL %s %s", role, username);
    send_line(socket_fd, enroll_line);
    response_len = 0;
    sent_apparate = 0;
    disconnect_reason = 4;
    while (1)
    {
        fd_set read_fds;
        int max_fd;
        int ready_count;

        if (client_stop_requested && !sent_apparate)
        {
            send_line(socket_fd, "APPARATE");
            print_client_event(role, username, "SENT", "APPARATE");
            sent_apparate = 1;
        }
        FD_ZERO(&read_fds);
        FD_SET(socket_fd, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        max_fd = socket_fd > STDIN_FILENO ? socket_fd : STDIN_FILENO;
        ready_count = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready_count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            disconnect_reason = 4;
            break;
        }
        if (FD_ISSET(socket_fd, &read_fds))
        {
            if (process_server_data(role, username, response_buf, &response_len, &disconnect_reason, socket_fd))
            {
                break;
            }
        }
        if (FD_ISSET(STDIN_FILENO, &read_fds) && !sent_apparate)
        {
            char command_line[MAX_LINE_LENGTH];

            if (fgets(command_line, sizeof(command_line), stdin) == NULL)
            {
                send_line(socket_fd, "APPARATE");
                print_client_event(role, username, "SENT", "APPARATE");
                sent_apparate = 1;
                continue;
            }
            strip_line_end(command_line);
            if (command_line[0] == '\0')
            {
                continue;
            }
            send_line(socket_fd, command_line);
            print_client_event(role, username, "SENT", command_line);
            if (strcmp(command_line, "APPARATE") == 0)
            {
                sent_apparate = 1;
            }
        }
    }
    close(socket_fd);
    print_disconnect_event(role, username, disconnect_reason);
    return 0;
}
