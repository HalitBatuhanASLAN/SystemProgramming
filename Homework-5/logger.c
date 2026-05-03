#include "logger.h"

#include <stdio.h>

/*
 * Initializes the mutex used only for console logging. All event lines pass
 * through this logger so courier output does not mix on stdout.
 */
int logger_init(logger_t *logger)
{
    /* This mutex is only for stdout lines. */
    return pthread_mutex_init(&logger->mutex, NULL) == 0;
}

/*
 * Destroys the log mutex after all threads are finished. No logging should
 * happen after this function is called.
 */
void logger_destroy(logger_t *logger)
{
    pthread_mutex_destroy(&logger->mutex);
}

/*
 * Prints the SHIFT_START line from the main program. It shows how many courier
 * threads will be created and how many valid orders were loaded.
 */
void log_shift_start(logger_t *logger, int courier_count, size_t order_count)
{
    /* All prints use the same lock to prevent mixed lines. */
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SHIFT_START couriers=%d orders=%zu\n", courier_count, order_count);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

/*
 * Prints one ORDER_QUEUED line after an order is inserted into the priority
 * queue. Invalid input lines are not printed because they are skipped.
 */
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

/*
 * Prints that a courier is waiting at the depot. This only happens when the
 * thread actually blocks on the condition variable, not in a busy loop.
 */
void log_waiting(logger_t *logger, int courier_id)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] WAITING\n", courier_id);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

/*
 * Prints the DELIVERY_START event for a courier. In courier.c this is called
 * while the queue lock is still held, so the printed order matches the pop
 * order from the priority queue.
 */
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

/*
 * Prints the DELIVERY_COMPLETE event after the simulated sleep is finished.
 * The duration is shown in milliseconds, so one input unit becomes 100ms.
 */
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

/*
 * Prints the line showing that one courier thread is leaving the shift. Every
 * courier prints this once before returning from its thread function.
 */
void log_shift_over(logger_t *logger, int courier_id)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[COURIER-%d] SHIFT_OVER\n", courier_id);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

/*
 * Prints the SIGINT_RECEIVED line after the main thread starts shutdown. The
 * pending_orders value is the number of orders removed from the queue.
 */
void log_sigint_received(logger_t *logger, size_t pending_orders)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SIGINT_RECEIVED pending_orders=%zu\n", pending_orders);
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}

/*
 * Prints one cancelled order during SIGINT shutdown. These are orders that had
 * not been started by any courier yet.
 */
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

/*
 * Prints the final SHIFT_END summary to stdout. The values come from atomic
 * counters so they are safe to read after the worker threads are done.
 */
void log_shift_end(logger_t *logger,
                   atomic_int *completed_orders,
                   atomic_int *cancelled_orders,
                   atomic_long *total_delivery_time)
{
    /* Read atomic counters once for the final line. */
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

/*
 * Prints the last required line. It is called only after the stats file has
 * been written successfully.
 */
void log_shutdown_complete(logger_t *logger)
{
    pthread_mutex_lock(&logger->mutex);
    printf("[CARGOGTU] SHUTDOWN_COMPLETE\n");
    fflush(stdout);
    pthread_mutex_unlock(&logger->mutex);
}
