#include "logger.h"

#include <stdio.h>

int logger_init(logger_t *logger)
{
    return pthread_mutex_init(&logger->mutex, NULL) == 0;
}

void logger_destroy(logger_t *logger)
{
    pthread_mutex_destroy(&logger->mutex);
}

void log_shift_start(logger_t *logger, int courier_count, size_t order_count)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SHIFT_START couriers=%d orders=%zu\n", courier_count, order_count);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_order_queued(logger_t *logger, const order_t *order)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] ORDER_QUEUED id=%d recipient=%s priority=%s duration=%d\n",
           order->id,
           order->recipient,
           priority_to_text(order->priority),
           order->duration_units);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_waiting(logger_t *logger, int courier_id)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] WAITING\n", courier_id);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_delivery_start(logger_t *logger, int courier_id, const order_t *order)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] DELIVERY_START id=%d recipient=%s priority=%s\n",
           courier_id,
           order->id,
           order->recipient,
           priority_to_text(order->priority));
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_delivery_complete(logger_t *logger, int courier_id, const order_t *order)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] DELIVERY_COMPLETE id=%d recipient=%s duration=%dms\n",
           courier_id,
           order->id,
           order->recipient,
           order->duration_units * 100);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_shift_over(logger_t *logger, int courier_id)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] SHIFT_OVER\n", courier_id);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_sigint_received(logger_t *logger, size_t pending_orders)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SIGINT_RECEIVED pending_orders=%zu\n", pending_orders);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_order_cancelled(logger_t *logger, const order_t *order)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] ORDER_CANCELLED id=%d recipient=%s priority=%s\n",
           order->id,
           order->recipient,
           priority_to_text(order->priority));
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_shift_end(logger_t *logger,
                   atomic_int *completed_orders,
                   atomic_int *cancelled_orders,
                   atomic_long *total_delivery_time)
{
    int completed = atomic_load(completed_orders);
    int cancelled = atomic_load(cancelled_orders);
    long total_time = atomic_load(total_delivery_time);

    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SHIFT_END completed=%d cancelled=%d total_time=%ldms\n",
           completed,
           cancelled,
           total_time);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

void log_shutdown_complete(logger_t *logger)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SHUTDOWN_COMPLETE\n");
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}
