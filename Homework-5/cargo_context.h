#ifndef CARGO_CONTEXT_H
#define CARGO_CONTEXT_H

#include "logger.h"
#include "priority_queue.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

typedef struct courier_stats
{
    int completed_orders;
    long total_delivery_time;
} courier_stats_t;

typedef struct cargo_context
{
    priority_queue_t queue;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    logger_t logger;
    int shutdown_requested;
    int active_deliveries;
    atomic_int completed_orders;
    atomic_int cancelled_orders;
    atomic_long total_delivery_time;
    courier_stats_t *courier_stats;
    int courier_count;
} cargo_context_t;

int cargo_context_init(cargo_context_t *context, int courier_count);
void cargo_context_destroy(cargo_context_t *context);
int cargo_context_add_order(cargo_context_t *context, const order_t *order);
void cargo_context_mark_completed(cargo_context_t *context, int courier_index, const order_t *order);
void cargo_context_finish_active_delivery(cargo_context_t *context);
int cargo_context_is_shift_complete(const cargo_context_t *context);
int cargo_context_cancel_pending_orders(cargo_context_t *context, order_t **orders, size_t *count);

#endif
