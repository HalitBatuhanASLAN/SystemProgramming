#include "priority_queue.h"

#include <stdlib.h>

/*
 * Swaps two order structs in the heap array. It is used while moving an item
 * up or down in the binary heap.
 */
static void swap_orders(order_t *left, order_t *right)
{
    order_t temp = *left;
    *left = *right;
    *right = temp;
}

/*
 * Makes sure the heap array has enough room for one more order. The capacity
 * grows by doubling, so pushing many orders does not reallocate every time.
 */
static int ensure_capacity(priority_queue_t *queue)
{
    order_t *new_items;
    size_t new_capacity;

    /* Current array still has free space. */
    if(queue->size < queue->capacity)
    {
        return 1;
    }

    new_capacity = queue->capacity == 0 ? 16 : queue->capacity * 2;
    new_items = realloc(queue->items, new_capacity * sizeof(order_t));
    if(new_items == NULL)
    {
        /* Caller handles the allocation failure. */
        return 0;
    }

    queue->items = new_items;
    queue->capacity = new_capacity;
    return 1;
}

/*
 * Moves a newly inserted order toward the root until heap order is correct.
 * This is called after adding the order to the end of the array.
 */
static void sift_up(priority_queue_t *queue, size_t index)
{
    /* Keep the smallest priority/id pair at the top. */
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

/*
 * Moves the root replacement down after a pop operation. At each step it picks
 * the better child according to priority and id, then swaps if needed.
 */
static void sift_down(priority_queue_t *queue, size_t index)
{
    /* Restore heap order after removing the root item. */
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

/*
 * Initializes an empty priority queue. The array is allocated lazily on the
 * first push, so init itself does not need heap memory.
 */
void priority_queue_init(priority_queue_t *queue)
{
    queue->items = NULL;
    queue->size = 0;
    queue->capacity = 0;
}

/*
 * Releases the internal heap array. It is safe for an empty queue too because
 * free(NULL) is valid.
 */
void priority_queue_destroy(priority_queue_t *queue)
{
    free(queue->items);
    queue->items = NULL;
    queue->size = 0;
    queue->capacity = 0;
}

/*
 * Inserts a new order into the priority queue. The queue itself does not lock;
 * callers lock queue_mutex before calling this in the threaded parts.
 */
int priority_queue_push(priority_queue_t *queue, const order_t *order)
{
    if(!ensure_capacity(queue))
    {
        return 0;
    }

    /* Put it at the end, then move it to correct place. */
    queue->items[queue->size] = *order;
    sift_up(queue, queue->size);
    queue->size++;
    return 1;
}

/*
 * Removes the best available order from the heap and copies it into order.
 * It returns 0 only when the queue is empty.
 */
int priority_queue_pop(priority_queue_t *queue, order_t *order)
{
    if(queue->size == 0)
    {
        /* Nothing to take from the queue. */
        return 0;
    }

    /* Root is the best order according to priority and id. */
    *order = queue->items[0];
    queue->size--;

    if(queue->size > 0)
    {
        /* Last item fills the root and heap is fixed again. */
        queue->items[0] = queue->items[queue->size];
        sift_down(queue, 0);
    }

    return 1;
}

/*
 * Returns the current number of pending orders. In the main program this value
 * is read while holding queue_mutex, because couriers may change it.
 */
size_t priority_queue_size(const priority_queue_t *queue)
{
    return queue->size;
}

/*
 * Removes all pending orders and returns them in priority order. This is used
 * during SIGINT shutdown so cancelled orders can be logged clearly.
 */
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

    /* Draining with pop keeps cancelled orders in scheduling order. */
    for(index = 0; queue->size > 0; index++)
    {
        priority_queue_pop(queue, &drained_orders[index]);
    }

    *orders = drained_orders;
    return 1;
}
