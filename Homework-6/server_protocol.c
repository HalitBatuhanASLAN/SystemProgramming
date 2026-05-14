#define _POSIX_C_SOURCE 200809L

#include "server_protocol.h"

#include "common.h"
#include "ingredients.h"
#include "server_clients.h"
#include "server_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static void send_unknown(int fd, const char *command)
{
    char response[MAX_LINE_LENGTH];

    snprintf(response, sizeof(response), "ERR UNKNOWN %s", command);
    send_line(fd, response);
}

static int send_memory_error(int fd)
{
    send_line(fd, "ERR INTERNAL");
    return 0;
}

static int handle_enroll(server_context_t *context, client_t *client, char **words, int word_count)
{
    char response[MAX_LINE_LENGTH];

    if (word_count != 3 || (strcmp(words[1], "WIZARD") != 0 && strcmp(words[1], "PROFESSOR") != 0) || words[2][0] == '\0' || strlen(words[2]) > MAX_USERNAME_LENGTH)
    {
        send_unknown(client->fd, "ENROLL");
        return 0;
    }
    if (username_exists(context, words[2]))
    {
        send_line(client->fd, "ERR ENROLL name_taken");
        return 0;
    }
    snprintf(client->type, sizeof(client->type), "%s", words[1]);
    snprintf(client->username, sizeof(client->username), "%s", words[2]);
    snprintf(response, sizeof(response), "OK ENROLL %s", client->username);
    send_line(client->fd, response);
    server_log_event(context, "ENROLL username=%s type=%s fd=%d", client->username, client->type, client->fd);
    return 0;
}

static int handle_brew(server_context_t *context, client_t *client, char **words, int word_count)
{
    int ingredient_index;
    long quantity;
    long old_quantity;
    char response[MAX_LINE_LENGTH];

    if (word_count != 3 || parse_long_value(words[2], 1, 2147483647L, &quantity) < 0)
    {
        send_unknown(client->fd, "BREW");
        return 0;
    }
    ingredient_index = ingredient_find_index(&context->ingredients, words[1]);
    if (ingredient_index < 0)
    {
        send_line(client->fd, "ERR UNKNOWN_INGREDIENT");
        return 0;
    }
    old_quantity = context->ingredients.items[ingredient_index].quantity;
    context->ingredients.items[ingredient_index].quantity += quantity;
    client->spellbook[ingredient_index] += quantity;
    snprintf(response, sizeof(response), "OK BREW %s %ld %ld", context->ingredients.items[ingredient_index].name, quantity, context->ingredients.items[ingredient_index].quantity);
    send_line(client->fd, response);
    server_log_event(context, "BREW wizard=%s ingredient=%s qty=%ld old_qty=%ld new_qty=%ld", client->username, context->ingredients.items[ingredient_index].name, quantity, old_quantity, context->ingredients.items[ingredient_index].quantity);
    return 0;
}

static int handle_consume(server_context_t *context, client_t *client, char **words, int word_count)
{
    int ingredient_index;
    long quantity;
    long old_quantity;
    char response[MAX_LINE_LENGTH];

    if (word_count != 3 || parse_long_value(words[2], 1, 2147483647L, &quantity) < 0)
    {
        send_unknown(client->fd, "CONSUME");
        return 0;
    }
    ingredient_index = ingredient_find_index(&context->ingredients, words[1]);
    if (ingredient_index < 0)
    {
        send_line(client->fd, "ERR UNKNOWN_INGREDIENT");
        return 0;
    }
    if (context->ingredients.items[ingredient_index].quantity < quantity || client->spellbook[ingredient_index] < quantity)
    {
        send_line(client->fd, "ERR INSUFFICIENT_INGREDIENTS");
        return 0;
    }
    old_quantity = context->ingredients.items[ingredient_index].quantity;
    context->ingredients.items[ingredient_index].quantity -= quantity;
    client->spellbook[ingredient_index] -= quantity;
    snprintf(response, sizeof(response), "OK CONSUME %s %ld %ld", context->ingredients.items[ingredient_index].name, quantity, context->ingredients.items[ingredient_index].quantity);
    send_line(client->fd, response);
    server_log_event(context, "CONSUME wizard=%s ingredient=%s qty=%ld old_qty=%ld new_qty=%ld", client->username, context->ingredients.items[ingredient_index].name, quantity, old_quantity, context->ingredients.items[ingredient_index].quantity);
    return 0;
}

static int handle_spellbook(server_context_t *context, client_t *client, int word_count)
{
    char *snapshot;
    char *response;
    size_t response_length;

    if (word_count != 1)
    {
        send_unknown(client->fd, "SPELLBOOK");
        return 0;
    }
    snapshot = spellbook_snapshot(&context->ingredients, client->spellbook);
    if (snapshot == NULL)
    {
        return send_memory_error(client->fd);
    }
    response_length = strlen(snapshot) + 16;
    response = malloc(response_length);
    if (response == NULL)
    {
        free(snapshot);
        return send_memory_error(client->fd);
    }
    snprintf(response, response_length, "OK SPELLBOOK %s", snapshot);
    send_line(client->fd, response);
    free(response);
    free(snapshot);
    return 0;
}

static int handle_inspect(server_context_t *context, client_t *client, char **words, int word_count)
{
    int ingredient_index;
    char response[MAX_LINE_LENGTH];

    if (word_count != 2)
    {
        send_unknown(client->fd, "INSPECT");
        return 0;
    }
    ingredient_index = ingredient_find_index(&context->ingredients, words[1]);
    if (ingredient_index < 0)
    {
        send_line(client->fd, "ERR UNKNOWN_INGREDIENT");
        return 0;
    }
    snprintf(response, sizeof(response), "OK INSPECT %s %ld", context->ingredients.items[ingredient_index].name, context->ingredients.items[ingredient_index].quantity);
    send_line(client->fd, response);
    server_log_event(context, "INSPECT professor=%s ingredient=%s qty=%ld", client->username, context->ingredients.items[ingredient_index].name, context->ingredients.items[ingredient_index].quantity);
    return 0;
}

static int handle_scroll(server_context_t *context, client_t *client, int word_count)
{
    char *snapshot;
    char *response;
    size_t response_length;

    if (word_count != 1)
    {
        send_unknown(client->fd, "SCROLL");
        return 0;
    }
    snapshot = ingredient_snapshot(&context->ingredients);
    if (snapshot == NULL)
    {
        return send_memory_error(client->fd);
    }
    response_length = strlen(snapshot) + 16;
    response = malloc(response_length);
    if (response == NULL)
    {
        free(snapshot);
        return send_memory_error(client->fd);
    }
    snprintf(response, response_length, "OK SCROLL %s", snapshot);
    send_line(client->fd, response);
    server_log_event(context, "SCROLL professor=%s ingredients=%zu", client->username, context->ingredients.count);
    free(response);
    free(snapshot);
    return 0;
}

static int handle_roster(server_context_t *context, client_t *client, int word_count)
{
    size_t needed;
    char *roster;
    char *response;
    size_t offset;
    int i;
    int listed_count;

    if (word_count != 1)
    {
        send_unknown(client->fd, "ROSTER");
        return 0;
    }
    needed = 6;
    listed_count = 0;
    for (i = 0; i < context->max_clients; i++)
    {
        if (context->clients[i].fd >= 0 && context->clients[i].username[0] != '\0')
        {
            needed += strlen(context->clients[i].username) + 2;
            listed_count++;
        }
    }
    roster = malloc(needed);
    if (roster == NULL)
    {
        return send_memory_error(client->fd);
    }
    offset = 0;
    roster[0] = '\0';
    if (listed_count == 0)
    {
        snprintf(roster, needed, "EMPTY");
    }
    else
    {
        for (i = 0; i < context->max_clients; i++)
        {
            if (context->clients[i].fd >= 0 && context->clients[i].username[0] != '\0')
            {
                int written;

                written = snprintf(roster + offset, needed - offset, "%s%s", offset == 0 ? "" : ",", context->clients[i].username);
                if (written < 0)
                {
                    free(roster);
                    return send_memory_error(client->fd);
                }
                offset += (size_t) written;
            }
        }
    }
    response = malloc(strlen(roster) + 16);
    if (response == NULL)
    {
        free(roster);
        return send_memory_error(client->fd);
    }
    snprintf(response, strlen(roster) + 16, "OK ROSTER %s", roster);
    send_line(client->fd, response);
    server_log_event(context, "ROSTER professor=%s clients=%d", client->username, listed_count);
    free(response);
    free(roster);
    return 0;
}

static int handle_apparate(server_context_t *context, int client_index)
{
    send_line(context->clients[client_index].fd, "OK APPARATE");
    close_client(context, client_index, "APPARATE");
    return 1;
}

static int handle_command(server_context_t *context, int client_index, const char *line)
{
    client_t *client;
    char command_copy[MAX_LINE_LENGTH];
    char *words[8];
    int word_count;

    client = &context->clients[client_index];
    snprintf(command_copy, sizeof(command_copy), "%s", line);
    word_count = split_words(command_copy, words, 8);
    if (word_count == 0)
    {
        send_unknown(client->fd, "");
        return 0;
    }
    if (client->username[0] == '\0')
    {
        if (strcmp(words[0], "ENROLL") == 0)
        {
            return handle_enroll(context, client, words, word_count);
        }
        send_line(client->fd, "ERR NOT_ENROLLED");
        return 0;
    }
    if (strcmp(words[0], "APPARATE") == 0)
    {
        if (word_count != 1)
        {
            send_unknown(client->fd, "APPARATE");
            return 0;
        }
        return handle_apparate(context, client_index);
    }
    if (strcmp(client->type, "WIZARD") == 0)
    {
        if (strcmp(words[0], "BREW") == 0)
        {
            return handle_brew(context, client, words, word_count);
        }
        if (strcmp(words[0], "CONSUME") == 0)
        {
            return handle_consume(context, client, words, word_count);
        }
        if (strcmp(words[0], "SPELLBOOK") == 0)
        {
            return handle_spellbook(context, client, word_count);
        }
        if (strcmp(words[0], "INSPECT") == 0 || strcmp(words[0], "SCROLL") == 0 || strcmp(words[0], "ROSTER") == 0)
        {
            send_line(client->fd, "ERR UNAUTHORIZED");
            return 0;
        }
        send_unknown(client->fd, words[0]);
        return 0;
    }
    if (strcmp(words[0], "INSPECT") == 0)
    {
        return handle_inspect(context, client, words, word_count);
    }
    if (strcmp(words[0], "SCROLL") == 0)
    {
        return handle_scroll(context, client, word_count);
    }
    if (strcmp(words[0], "ROSTER") == 0)
    {
        return handle_roster(context, client, word_count);
    }
    if (strcmp(words[0], "BREW") == 0 || strcmp(words[0], "CONSUME") == 0 || strcmp(words[0], "SPELLBOOK") == 0)
    {
        send_line(client->fd, "ERR UNAUTHORIZED");
        return 0;
    }
    send_unknown(client->fd, words[0]);
    return 0;
}

int process_client_bytes(server_context_t *context, int client_index)
{
    char input_buf[256];
    ssize_t read_count;
    ssize_t i;
    client_t *client;

    client = &context->clients[client_index];
    read_count = recv(client->fd, input_buf, sizeof(input_buf), 0);
    if (read_count < 0)
    {
        if (errno == EINTR)
        {
            return 0;
        }
        close_client(context, client_index, "hangup");
        return 1;
    }
    if (read_count == 0)
    {
        close_client(context, client_index, "hangup");
        return 1;
    }
    client->last_active = time(NULL);
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
            if (client->line_too_long)
            {
                send_line(client->fd, "ERR TOOLONG");
                client->line_too_long = 0;
                client->line_len = 0;
                continue;
            }
            client->line_buf[client->line_len] = '\0';
            if (handle_command(context, client_index, client->line_buf))
            {
                return 1;
            }
            if (context->clients[client_index].fd < 0)
            {
                return 1;
            }
            context->clients[client_index].line_len = 0;
        }
        else
        {
            if (client->line_len + 1 >= MAX_LINE_LENGTH)
            {
                client->line_too_long = 1;
            }
            else if (!client->line_too_long)
            {
                client->line_buf[client->line_len] = current;
                client->line_len++;
            }
        }
    }
    return 0;
}
