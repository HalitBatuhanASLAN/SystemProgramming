#include "cargo_context.h"

#include <stdlib.h>

int cargo_context_init(cargo_context_t *context, int courier_count)
{
    priority_queue_init(&context->queue);
    context->shutdown_requested = 0;
    context->active_deliveries = 0;
    context->courier_count = courier_count;
    context->courier_stats = calloc((size_t)courier_count, sizeof(courier_stats_t));

    if(context->courier_stats == NULL)
    {
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

void cargo_context_destroy(cargo_context_t *context)
{
    logger_destroy(&context->logger);
    pthread_cond_destroy(&context->queue_cond);
    pthread_mutex_destroy(&context->queue_mutex);
    priority_queue_destroy(&context->queue);
    free(context->courier_stats);
    context->courier_stats = NULL;
}

int cargo_context_add_order(cargo_context_t *context, const order_t *order)
{
    int result;

    pthread_mutex_lock(&context->queue_mutex);
    result = priority_queue_push(&context->queue, order);
    pthread_cond_signal(&context->queue_cond);
    pthread_mutex_unlock(&context->queue_mutex);

    return result;
}

void cargo_context_mark_completed(cargo_context_t *context, int courier_index, const order_t *order)
{
    int duration_ms = order->duration_units * 100;

    atomic_fetch_add(&context->completed_orders, 1);
    atomic_fetch_add(&context->total_delivery_time, duration_ms);
    context->courier_stats[courier_index].completed_orders++;
    context->courier_stats[courier_index].total_delivery_time += duration_ms;
}

void cargo_context_finish_active_delivery(cargo_context_t *context)
{
    pthread_mutex_lock(&context->queue_mutex);
    context->active_deliveries--;

    if(context->active_deliveries == 0 || context->shutdown_requested)
    {
        pthread_cond_broadcast(&context->queue_cond);
    }

    pthread_mutex_unlock(&context->queue_mutex);
}

int cargo_context_is_shift_complete(const cargo_context_t *context)
{
    return priority_queue_size(&context->queue) == 0 && context->active_deliveries == 0;
}

int cargo_context_cancel_pending_orders(cargo_context_t *context, order_t **orders, size_t *count)
{
    int result;

    pthread_mutex_lock(&context->queue_mutex);
    context->shutdown_requested = 1;
    result = priority_queue_drain(&context->queue, orders, count);
    pthread_cond_broadcast(&context->queue_cond);
    pthread_mutex_unlock(&context->queue_mutex);

    return result;
}
