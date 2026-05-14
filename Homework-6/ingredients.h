#ifndef INGREDIENTS_H
#define INGREDIENTS_H

#include <stddef.h>

#include "common.h"

typedef struct
{
    char name[MAX_INGREDIENT_NAME_LENGTH + 1];
    long quantity;
} ingredient_t;

typedef struct
{
    ingredient_t *items;
    size_t count;
    size_t capacity;
} ingredient_table_t;

void ingredient_table_init(ingredient_table_t *table);
void ingredient_table_free(ingredient_table_t *table);
int ingredient_table_load(ingredient_table_t *table, const char *path);
int ingredient_find_index(const ingredient_table_t *table, const char *name);
char *ingredient_snapshot(const ingredient_table_t *table);
char *spellbook_snapshot(const ingredient_table_t *table, const long *spellbook);

#endif
