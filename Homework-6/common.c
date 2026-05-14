#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int send_all(int fd, const char *data, size_t length)
{
    size_t sent_total;

    // TCP can write less bytes, so this loop sends remaining part again.
    sent_total = 0;
    while (sent_total < length)
    {
        ssize_t sent_now;

        sent_now = send(fd, data + sent_total, length - sent_total, 0);
        if (sent_now < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (sent_now == 0)
        {
            return -1;
        }
        sent_total += (size_t) sent_now;
    }
    return 0;
}

int send_line(int fd, const char *line)
{
    // Protocol is line based, every message must finish with newline.
    if (send_all(fd, line, strlen(line)) < 0)
    {
        return -1;
    }
    return send_all(fd, "\n", 1);
}

int parse_int_value(const char *text, int min_value, int max_value, int *value)
{
    char *end_ptr;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end_ptr, 10);
    if (errno != 0 || end_ptr == text || *end_ptr != '\0')
    {
        return -1;
    }
    if (parsed < min_value || parsed > max_value)
    {
        return -1;
    }
    *value = (int) parsed;
    return 0;
}

int parse_long_value(const char *text, long min_value, long max_value, long *value)
{
    char *end_ptr;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end_ptr, 10);
    if (errno != 0 || end_ptr == text || *end_ptr != '\0')
    {
        return -1;
    }
    if (parsed < min_value || parsed > max_value)
    {
        return -1;
    }
    *value = parsed;
    return 0;
}

void strip_line_end(char *line)
{
    size_t length;

    length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    {
        line[length - 1] = '\0';
        length--;
    }
}

int split_words(char *line, char **words, int max_words)
{
    int count;
    char *token;

    count = 0;
    // Simple whitespace split is enough for this homework commands.
    token = strtok(line, " \t\r\n");
    while (token != NULL && count < max_words)
    {
        words[count] = token;
        count++;
        token = strtok(NULL, " \t\r\n");
    }
    return count;
}

int is_valid_ingredient_name(const char *name)
{
    size_t i;
    size_t length;

    // Ingredient names must stay uppercase like PDF says.
    length = strlen(name);
    if (length == 0 || length > MAX_INGREDIENT_NAME_LENGTH)
    {
        return 0;
    }
    for (i = 0; i < length; i++)
    {
        unsigned char current;

        current = (unsigned char) name[i];
        if (!(isupper(current) || isdigit(current) || current == '_'))
        {
            return 0;
        }
    }
    return 1;
}
