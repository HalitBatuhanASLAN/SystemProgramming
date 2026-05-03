#ifndef PRIORITY_QUEUE_H
#define PRIORITY_QUEUE_H

#include "order.h"

#include <stddef.h>

typedef struct priority_queue
{
    order_t *items;
    size_t size;
    size_t capacity;
} priority_queue_t;

void priority_queue_init(priority_queue_t *queue);
void priority_queue_destroy(priority_queue_t *queue);
int priority_queue_push(priority_queue_t *queue, const order_t *order);
int priority_queue_pop(priority_queue_t *queue, order_t *order);
size_t priority_queue_size(const priority_queue_t *queue);
int priority_queue_drain(priority_queue_t *queue, order_t **orders, size_t *count);

#endif
