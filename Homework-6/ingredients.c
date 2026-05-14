#define _POSIX_C_SOURCE 200809L

#include "ingredients.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ingredient_table_init(ingredient_table_t *table)
{
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

void ingredient_table_free(ingredient_table_t *table)
{
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

static int ingredient_table_reserve(ingredient_table_t *table, size_t new_capacity)
{
    ingredient_t *new_items;

    // Ingredient array grows when file has more valid lines.
    new_items = realloc(table->items, new_capacity * sizeof(ingredient_t));
    if (new_items == NULL)
    {
        return -1;
    }
    table->items = new_items;
    table->capacity = new_capacity;
    return 0;
}

int ingredient_find_index(const ingredient_table_t *table, const char *name)
{
    size_t i;

    // Linear search is okay here because ingredient list is small.
    for (i = 0; i < table->count; i++)
    {
        if (strcmp(table->items[i].name, name) == 0)
        {
            return (int) i;
        }
    }
    return -1;
}

int ingredient_table_load(ingredient_table_t *table, const char *path)
{
    FILE *file;
    char line[256];

    // Invalid ingredient lines are just skipped, no error printed.
    file = fopen(path, "r");
    if (file == NULL)
    {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char name[64];
        char quantity_text[64];
        char extra[8];
        long quantity;
        int field_count;

        field_count = sscanf(line, "%63s %63s %7s", name, quantity_text, extra);
        if (field_count != 2)
        {
            continue;
        }
        if (!is_valid_ingredient_name(name))
        {
            continue;
        }
        if (parse_long_value(quantity_text, 1, 2147483647L, &quantity) < 0)
        {
            continue;
        }
        if (ingredient_find_index(table, name) >= 0)
        {
            continue;
        }
        if (table->count == table->capacity)
        {
            size_t new_capacity;

            new_capacity = table->capacity == 0 ? 8 : table->capacity * 2;
            if (ingredient_table_reserve(table, new_capacity) < 0)
            {
                fclose(file);
                return -1;
            }
        }
        memcpy(table->items[table->count].name, name, strlen(name) + 1);
        table->items[table->count].quantity = quantity;
        table->count++;
    }
    fclose(file);
    return 0;
}

char *ingredient_snapshot(const ingredient_table_t *table)
{
    size_t i;
    size_t needed;
    char *snapshot;
    size_t offset;

    // Snapshot is created as INGREDIENT:qty,INGREDIENT:qty form.
    needed = 1;
    for (i = 0; i < table->count; i++)
    {
        needed += strlen(table->items[i].name) + 32;
    }
    if (table->count == 0)
    {
        needed += 5;
    }
    snapshot = malloc(needed);
    if (snapshot == NULL)
    {
        return NULL;
    }
    offset = 0;
    if (table->count == 0)
    {
        snprintf(snapshot, needed, "EMPTY");
        return snapshot;
    }
    snapshot[0] = '\0';
    for (i = 0; i < table->count; i++)
    {
        int written;

        written = snprintf(snapshot + offset, needed - offset, "%s%s:%ld", i == 0 ? "" : ",", table->items[i].name, table->items[i].quantity);
        if (written < 0)
        {
            free(snapshot);
            return NULL;
        }
        offset += (size_t) written;
    }
    return snapshot;
}

char *spellbook_snapshot(const ingredient_table_t *table, const long *spellbook)
{
    size_t i;
    size_t needed;
    char *snapshot;
    size_t offset;
    int has_item;

    // Only positive spellbook amounts are printed to client.
    needed = 1;
    has_item = 0;
    for (i = 0; i < table->count; i++)
    {
        if (spellbook[i] > 0)
        {
            needed += strlen(table->items[i].name) + 32;
            has_item = 1;
        }
    }
    if (!has_item)
    {
        needed += 5;
    }
    snapshot = malloc(needed);
    if (snapshot == NULL)
    {
        return NULL;
    }
    if (!has_item)
    {
        snprintf(snapshot, needed, "EMPTY");
        return snapshot;
    }
    offset = 0;
    snapshot[0] = '\0';
    for (i = 0; i < table->count; i++)
    {
        if (spellbook[i] > 0)
        {
            int written;

            written = snprintf(snapshot + offset, needed - offset, "%s%s:%ld", offset == 0 ? "" : ",", table->items[i].name, spellbook[i]);
            if (written < 0)
            {
                free(snapshot);
                return NULL;
            }
            offset += (size_t) written;
        }
    }
    return snapshot;
}
