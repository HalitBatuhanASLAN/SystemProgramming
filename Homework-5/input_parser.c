#include "input_parser.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ensure_input_capacity(input_orders_t *input_orders)
{
    order_t *new_orders;
    size_t new_capacity;

    if(input_orders->count < input_orders->capacity)
    {
        return 1;
    }

    new_capacity = input_orders->capacity == 0 ? 16 : input_orders->capacity * 2;
    new_orders = realloc(input_orders->orders, new_capacity * sizeof(order_t));
    if(new_orders == NULL)
    {
        return 0;
    }

    input_orders->orders = new_orders;
    input_orders->capacity = new_capacity;
    return 1;
}

static int parse_order_line(const char *line, order_t *order)
{
    char recipient[MAX_RECIPIENT_LENGTH + 1];
    char priority_text[16];
    char extra;
    long id;
    long duration;
    priority_t priority;
    int fields;

    fields = sscanf(line, " %ld %32s %15s %ld %c", &id, recipient, priority_text, &duration, &extra);
    if(fields != 4)
    {
        return 0;
    }

    if(id < 1 || id > 2147483647L || duration < 1 || duration > 21474836L)
    {
        return 0;
    }

    if(!order_has_valid_recipient(recipient))
    {
        return 0;
    }

    if(!priority_from_text(priority_text, &priority))
    {
        return 0;
    }

    order->id = (int)id;
    strcpy(order->recipient, recipient);
    order->priority = priority;
    order->duration_units = (int)duration;
    return 1;
}

void input_orders_init(input_orders_t *input_orders)
{
    input_orders->orders = NULL;
    input_orders->count = 0;
    input_orders->capacity = 0;
}

void input_orders_destroy(input_orders_t *input_orders)
{
    free(input_orders->orders);
    input_orders->orders = NULL;
    input_orders->count = 0;
    input_orders->capacity = 0;
}

int read_input_orders(const char *path, input_orders_t *input_orders)
{
    FILE *file;
    char line[512];

    file = fopen(path, "r");
    if(file == NULL)
    {
        return 0;
    }

    while(fgets(line, sizeof(line), file) != NULL)
    {
        order_t order;

        if(parse_order_line(line, &order))
        {
            if(!ensure_input_capacity(input_orders))
            {
                fclose(file);
                return 0;
            }

            input_orders->orders[input_orders->count] = order;
            input_orders->count++;
        }
    }

    if(ferror(file))
    {
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}
