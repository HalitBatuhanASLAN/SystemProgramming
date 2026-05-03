#ifndef INPUT_PARSER_H
#define INPUT_PARSER_H

#include "priority_queue.h"

#include <stddef.h>

typedef struct input_orders
{
    order_t *orders;
    size_t count;
    size_t capacity;
} input_orders_t;

void input_orders_init(input_orders_t *input_orders);
void input_orders_destroy(input_orders_t *input_orders);
int read_input_orders(const char *path, input_orders_t *input_orders);

#endif
