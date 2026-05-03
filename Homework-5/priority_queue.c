#include "priority_queue.h"

#include <stdlib.h>

static void swap_orders(order_t *left, order_t *right)
{
    order_t temp = *left;
    *left = *right;
    *right = temp;
}

static int ensure_capacity(priority_queue_t *queue)
{
    order_t *new_items;
    size_t new_capacity;

    if(queue->size < queue->capacity)
    {
        return 1;
    }

    new_capacity = queue->capacity == 0 ? 16 : queue->capacity * 2;
    new_items = realloc(queue->items, new_capacity * sizeof(order_t));
    if(new_items == NULL)
    {
        return 0;
    }

    queue->items = new_items;
    queue->capacity = new_capacity;
    return 1;
}

static void sift_up(priority_queue_t *queue, size_t index)
{
    while(index > 0)
    {
        size_t parent = (index - 1) / 2;

        if(order_compare(&queue->items[index], &queue->items[parent]) >= 0)
        {
            break;
        }

        swap_orders(&queue->items[index], &queue->items[parent]);
        index = parent;
    }
}

static void sift_down(priority_queue_t *queue, size_t index)
{
    while(1)
    {
        size_t left = index * 2 + 1;
        size_t right = left + 1;
        size_t smallest = index;

        if(left < queue->size &&
           order_compare(&queue->items[left], &queue->items[smallest]) < 0)
        {
            smallest = left;
        }

        if(right < queue->size &&
           order_compare(&queue->items[right], &queue->items[smallest]) < 0)
        {
            smallest = right;
        }

        if(smallest == index)
        {
            break;
        }

        swap_orders(&queue->items[index], &queue->items[smallest]);
        index = smallest;
    }
}

void priority_queue_init(priority_queue_t *queue)
{
    queue->items = NULL;
    queue->size = 0;
    queue->capacity = 0;
}

void priority_queue_destroy(priority_queue_t *queue)
{
    free(queue->items);
    queue->items = NULL;
    queue->size = 0;
    queue->capacity = 0;
}

int priority_queue_push(priority_queue_t *queue, const order_t *order)
{
    if(!ensure_capacity(queue))
    {
        return 0;
    }

    queue->items[queue->size] = *order;
    sift_up(queue, queue->size);
    queue->size++;
    return 1;
}

int priority_queue_pop(priority_queue_t *queue, order_t *order)
{
    if(queue->size == 0)
    {
        return 0;
    }

    *order = queue->items[0];
    queue->size--;

    if(queue->size > 0)
    {
        queue->items[0] = queue->items[queue->size];
        sift_down(queue, 0);
    }

    return 1;
}

size_t priority_queue_size(const priority_queue_t *queue)
{
    return queue->size;
}

int priority_queue_drain(priority_queue_t *queue, order_t **orders, size_t *count)
{
    size_t index;
    order_t *drained_orders;

    *count = queue->size;
    *orders = NULL;

    if(queue->size == 0)
    {
        return 1;
    }

    drained_orders = malloc(queue->size * sizeof(order_t));
    if(drained_orders == NULL)
    {
        return 0;
    }

    for(index = 0; queue->size > 0; index++)
    {
        priority_queue_pop(queue, &drained_orders[index]);
    }

    *orders = drained_orders;
    return 1;
}
