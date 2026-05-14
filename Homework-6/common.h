#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

#define MAX_LINE_LENGTH 512
#define MAX_USERNAME_LENGTH 31
#define MAX_TYPE_LENGTH 15
#define MAX_INGREDIENT_NAME_LENGTH 16

int send_all(int fd, const char *data, size_t length);
int send_line(int fd, const char *line);
int parse_int_value(const char *text, int min_value, int max_value, int *value);
int parse_long_value(const char *text, long min_value, long max_value, long *value);
void strip_line_end(char *line);
int split_words(char *line, char **words, int max_words);
int is_valid_ingredient_name(const char *name);

#endif
