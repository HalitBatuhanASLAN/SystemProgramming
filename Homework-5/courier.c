#include "courier.h"

#include "signal_state.h"

#include <errno.h>
#include <time.h>

static void sleep_for_delivery(int duration_units)
{
    struct timespec remaining;
    struct timespec request;
    long duration_ms = (long)duration_units * 100L;

    request.tv_sec = duration_ms / 1000L;
    request.tv_nsec = (duration_ms % 1000L) * 1000000L;

    while(nanosleep(&request, &remaining) == -1)
    {
        if(errno != EINTR)
        {
            break;
        }

        request = remaining;
    }
}

static int take_next_order(cargo_context_t *context, int courier_id, order_t *order)
{
    pthread_mutex_lock(&context->queue_mutex);

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
        pthread_mutex_unlock(&context->queue_mutex);
        return 0;
    }

    priority_queue_pop(&context->queue, order);
    context->active_deliveries++;
    log_delivery_start(&context->logger, courier_id, order);
    pthread_mutex_unlock(&context->queue_mutex);

    return 1;
}

void *courier_thread_main(void *arg)
{
    courier_args_t *courier_args = arg;
    cargo_context_t *context = courier_args->context;
    order_t order;

    while(take_next_order(context, courier_args->courier_id, &order))
    {
        sleep_for_delivery(order.duration_units);
        cargo_context_mark_completed(context, courier_args->courier_index, &order);
        log_delivery_complete(&context->logger, courier_args->courier_id, &order);
        cargo_context_finish_active_delivery(context);
    }

    log_shift_over(&context->logger, courier_args->courier_id);
    return NULL;
}
