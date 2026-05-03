#include "courier.h"

#include "signal_state.h"

#include <errno.h>
#include <time.h>

/*
 * Sleeps for the simulated delivery duration. If nanosleep is interrupted by
 * SIGINT, it continues sleeping for the remaining time, because active delivery
 * must not be interrupted midway.
 */
static void sleep_for_delivery(int duration_units)
{
    struct timespec remaining;
    struct timespec request;
    long duration_ms = (long)duration_units * 100L;

    /* One simulation unit means 100 milliseconds. */
    request.tv_sec = duration_ms / 1000L;
    request.tv_nsec = (duration_ms % 1000L) * 1000000L;

    /* A started delivery is completed even if SIGINT arrives. */
    while(nanosleep(&request, &remaining) == -1)
    {
        if(errno != EINTR)
        {
            break;
        }

        request = remaining;
    }
}

/*
 * Takes the next order for one courier. It blocks on the condition variable
 * when there is no pending order but some other courier is still delivering.
 * It returns 1 when an order was taken, otherwise 0 to end the courier loop.
 */
static int take_next_order(cargo_context_t *context, int courier_id, order_t *order)
{
    pthread_mutex_lock(&context->queue_mutex);

    /* Couriers block instead of polling while other deliveries are active. */
    while(priority_queue_size(&context->queue) == 0 &&
          !context->shutdown_requested &&
          !signal_state_sigint_requested() &&
          context->active_deliveries > 0)
    {
        log_waiting(&context->logger, courier_id);
        pthread_cond_wait(&context->queue_cond, &context->queue_mutex);
    }

    if(context->shutdown_requested ||
       signal_state_sigint_requested() ||
       priority_queue_size(&context->queue) == 0)
    {
        /* No new job is taken after shutdown starts. */
        pthread_mutex_unlock(&context->queue_mutex);
        return 0;
    }

    /* Start line is printed while the selected order is still protected. */
    priority_queue_pop(&context->queue, order);
    context->active_deliveries++;
    log_delivery_start(&context->logger, courier_id, order);
    pthread_mutex_unlock(&context->queue_mutex);

    return 1;
}

/*
 * Main function of a courier thread. A courier keeps taking orders from the
 * shared queue, delivers them, updates stats, and exits only when the shift is
 * finished or shutdown mode starts.
 */
void *courier_thread_main(void *arg)
{
    courier_args_t *courier_args = arg;
    cargo_context_t *context = courier_args->context;
    order_t order;

    /* Courier keeps working until queue is done or shutdown begins. */
    while(take_next_order(context, courier_args->courier_id, &order))
    {
        sleep_for_delivery(order.duration_units);
        /* Stats are updated after delivery really ends. */
        cargo_context_mark_completed(context, courier_args->courier_index, &order);
        log_delivery_complete(&context->logger, courier_args->courier_id, &order);
        cargo_context_finish_active_delivery(context);
    }

    /* Every courier prints this before thread exits. */
    log_shift_over(&context->logger, courier_args->courier_id);
    return NULL;
}
