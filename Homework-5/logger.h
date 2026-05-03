#ifndef LOGGER_H
#define LOGGER_H

#include "order.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>

typedef struct logger
{
    pthread_mutex_t mutex;
} logger_t;

int logger_init(logger_t *logger);
void logger_destroy(logger_t *logger);
void log_shift_start(logger_t *logger, int courier_count, size_t order_count);
void log_order_queued(logger_t *logger, const order_t *order);
void log_waiting(logger_t *logger, int courier_id);
void log_delivery_start(logger_t *logger, int courier_id, const order_t *order);
void log_delivery_complete(logger_t *logger, int courier_id, const order_t *order);
void log_shift_over(logger_t *logger, int courier_id);
void log_sigint_received(logger_t *logger, size_t pending_orders);
void log_order_cancelled(logger_t *logger, const order_t *order);
void log_shift_end(logger_t *logger,
                   atomic_int *completed_orders,
                   atomic_int *cancelled_orders,
                   atomic_long *total_delivery_time);
void log_shutdown_complete(logger_t *logger);

#endif
