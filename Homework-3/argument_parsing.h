#ifndef ARGUMENT_PARSING_H
#define ARGUMENT_PARSING_H

#include "common.h"

/* Komut satiri argumanlarini parse eder.
 * Basarili: 0, Hata: -1 (stderr'e usage yazilir) */
int parse_args(int argc, char *argv[], SystemConfig *config);

/* Usage bilgisini stderr'e yazdirir */
void print_usage(const char *prog_name);

#endif /* ARGUMENT_PARSING_H */