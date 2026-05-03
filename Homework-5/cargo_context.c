#include "cargo_context.h"

#include <stdlib.h>

/*
 * Initializes all shared state used by the simulation. This includes the
 * priority queue, mutexes, condition variable, logger, atomic counters, and
 * per-courier statistics array.
 */
int cargo_context_init(cargo_context_t *context, int courier_count)
{
    /* Shared data used by main and courier threads. */
    priority_queue_init(&context->queue);
    context->shutdown_requested = 0;
    context->active_deliveries = 0;
    context->courier_count = courier_count;
    context->courier_stats = calloc((size_t)courier_count, sizeof(courier_stats_t));

    if(context->courier_stats == NULL)
    {
        /* Per-courier stats are kept outside the atomic totals. */
        return 0;
    }

    if(pthread_mutex_init(&context->queue_mutex, NULL) != 0)
    {
        free(context->courier_stats);
        context->courier_stats = NULL;
        return 0;
    }

    if(pthread_cond_init(&context->queue_cond, NULL) != 0)
    {
        pthread_mutex_destroy(&context->queue_mutex);
        free(context->courier_stats);
        context->courier_stats = NULL;
        return 0;
    }

    if(!logger_init(&context->logger))
    {
        pthread_cond_destroy(&context->queue_cond);
        pthread_mutex_destroy(&context->queue_mutex);
        free(context->courier_stats);
        context->courier_stats = NULL;
        return 0;
    }

    atomic_init(&context->completed_orders, 0);
    atomic_init(&context->cancelled_orders, 0);
    atomic_init(&context->total_delivery_time, 0);
    return 1;
}

/*
 * Cleans up the shared simulation state after every courier thread has joined.
 * It frees heap memory and destroys synchronization primitives.
 */
void cargo_context_destroy(cargo_context_t *context)
{
    /* Destroy in reverse order of init. */
    logger_destroy(&context->logger);
    pthread_cond_destroy(&context->queue_cond);
    pthread_mutex_destroy(&context->queue_mutex);
    priority_queue_destroy(&context->queue);
    free(context->courier_stats);
    context->courier_stats = NULL;
}

/*
 * Adds one parsed order into the shared priority queue. It locks queue_mutex
 * because the queue is a shared object, even though startup queues orders before
 * worker threads are created.
 */
int cargo_context_add_order(cargo_context_t *context, const order_t *order)
{
    int result;

    /* Queue is shared, so insertion is locked. */
    pthread_mutex_lock(&context->queue_mutex);
    result = priority_queue_push(&context->queue, order);
    pthread_cond_signal(&context->queue_cond);
    pthread_mutex_unlock(&context->queue_mutex);

    return result;
}

/*
 * Updates statistics after a courier completes an order. Global totals are
 * atomic as requested, and the courier's own stats are updated by that courier.
 */
void cargo_context_mark_completed(cargo_context_t *context, int courier_index, const order_t *order)
{
    int duration_ms = order->duration_units * 100;

    /* Required global counters are atomic. */
    atomic_fetch_add(&context->completed_orders, 1);
    atomic_fetch_add(&context->total_delivery_time, duration_ms);
    context->courier_stats[courier_index].completed_orders++;
    context->courier_stats[courier_index].total_delivery_time += duration_ms;
}

/*
 * Decreases active_deliveries when a courier returns to the depot. If this was
 * the last active delivery, the main thread and waiting couriers are woken up.
 */
void cargo_context_finish_active_delivery(cargo_context_t *context)
{
    pthread_mutex_lock(&context->queue_mutex);
    context->active_deliveries--;

    /* Wake the main thread when the last active courier returns. */
    if(context->active_deliveries == 0 || context->shutdown_requested)
    {
        pthread_cond_broadcast(&context->queue_cond);
    }

    pthread_mutex_unlock(&context->queue_mutex);
}

/*
 * Checks whether there is no pending order and no active delivery. The caller
 * holds queue_mutex while using this, so the two values are checked together.
 */
int cargo_context_is_shift_complete(const cargo_context_t *context)
{
    return priority_queue_size(&context->queue) == 0 && context->active_deliveries == 0;
}

/*
 * Starts shutdown mode and removes all orders that were still waiting. The
 * removed orders are returned to the caller so they can be counted and logged.
 */
int cargo_context_cancel_pending_orders(cargo_context_t *context, order_t **orders, size_t *count)
{
    int result;

    /* Shutdown changes queue state and wakes idle couriers. */
    pthread_mutex_lock(&context->queue_mutex);
    context->shutdown_requested = 1;
    result = priority_queue_drain(&context->queue, orders, count);
    pthread_cond_broadcast(&context->queue_cond);
    pthread_mutex_unlock(&context->queue_mutex);

    return result;
}
