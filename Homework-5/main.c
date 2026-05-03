#include "cargo_context.h"
#include "cli.h"
#include "courier.h"
#include "input_parser.h"
#include "signal_state.h"
#include "stats_writer.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Builds an absolute timeout for pthread_cond_timedwait. The main thread uses
 * short waits so it can notice the SIGINT flag without doing any work inside
 * the signal handler.
 */
static void add_milliseconds_to_now(struct timespec *deadline, long milliseconds)
{
    /* Make a small deadline for the main wait loop. */
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += milliseconds / 1000L;
    deadline->tv_nsec += (milliseconds % 1000L) * 1000000L;

    /* Normalize nanoseconds if it passed one second. */
    if(deadline->tv_nsec >= 1000000000L)
    {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

/*
 * Waits until either all queued/active work is finished or SIGINT is received.
 * This function is used by the main thread after all couriers have started.
 */
static void wait_until_shift_finishes_or_sigint(cargo_context_t *context)
{
    struct timespec deadline;

    /* Main checks the shared state while holding the queue lock. */
    pthread_mutex_lock(&context->queue_mutex);

    /* Timed waits let the main thread notice the SIGINT flag promptly. */
    while(!cargo_context_is_shift_complete(context) &&
          !signal_state_sigint_requested())
    {
        add_milliseconds_to_now(&deadline, 100L);
        pthread_cond_timedwait(&context->queue_cond, &context->queue_mutex, &deadline);
    }

    pthread_cond_broadcast(&context->queue_cond);
    pthread_mutex_unlock(&context->queue_mutex);
}

/*
 * Performs the real shutdown work after SIGINT. It cancels pending orders,
 * logs them, updates the cancelled counter, and wakes courier threads.
 */
static int handle_sigint_shutdown(cargo_context_t *context)
{
    order_t *cancelled_orders = NULL;
    size_t cancelled_count = 0;
    size_t index;

    if(!signal_state_sigint_requested())
    {
        return 1;
    }

    /* After SIGINT, queued orders are not delivered anymore. */
    if(!cargo_context_cancel_pending_orders(context, &cancelled_orders, &cancelled_count))
    {
        fprintf(stderr, "Failed to cancel pending orders cleanly.\n");
        return 0;
    }

    log_sigint_received(&context->logger, cancelled_count);

    /* Print and count every cancelled order one by one. */
    for(index = 0; index < cancelled_count; index++)
    {
        atomic_fetch_add(&context->cancelled_orders, 1);
        log_order_cancelled(&context->logger, &cancelled_orders[index]);
    }

    free(cancelled_orders);
    return 1;
}

/*
 * Inserts all parsed input orders into the shared priority queue. This is done
 * before courier threads start, which matches the homework requirement.
 */
static int queue_all_orders(cargo_context_t *context, const input_orders_t *input_orders)
{
    size_t index;

    /* Orders are already parsed, here we only put them into the heap. */
    for(index = 0; index < input_orders->count; index++)
    {
        if(!cargo_context_add_order(context, &input_orders->orders[index]))
        {
            fprintf(stderr, "Failed to queue order %d.\n", input_orders->orders[index].id);
            return 0;
        }

        log_order_queued(&context->logger, &input_orders->orders[index]);
    }

    return 1;
}

/*
 * Creates the fixed courier thread pool. The function returns the number of
 * threads that were created, so main can join only those if a create call fails.
 */
static int start_couriers(cargo_context_t *context, pthread_t *threads, courier_args_t *args)
{
    int index;

    /* Thread pool is created once at shift start. */
    for(index = 0; index < context->courier_count; index++)
    {
        args[index].context = context;
        args[index].courier_id = index + 1;
        args[index].courier_index = index;

        if(pthread_create(&threads[index], NULL, courier_thread_main, &args[index]) != 0)
        {
            fprintf(stderr, "Failed to create courier thread %d.\n", index + 1);
            return index;
        }
    }

    return context->courier_count;
}

/*
 * Joins every courier thread that was successfully started. The program does
 * not write the statistics file until all of these joins are complete.
 */
static void join_couriers(pthread_t *threads, int started_count)
{
    int index;

    /* Wait all started couriers before writing stats file. */
    for(index = 0; index < started_count; index++)
    {
        pthread_join(threads[index], NULL);
    }
}

/*
 * Program entry point. It parses arguments, loads orders, starts the courier
 * pool, handles normal or SIGINT shutdown, writes the statistics file, and
 * finally releases all allocated resources.
 */
int main(int argc, char **argv)
{
    program_options_t options;
    input_orders_t input_orders;
    cargo_context_t context;
    pthread_t *threads = NULL;
    courier_args_t *courier_args = NULL;
    int started_count = 0;
    int exit_code = EXIT_FAILURE;

    if(!parse_program_options(argc, argv, &options))
    {
        /* Wrong command line, so only usage is printed. */
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if(!install_sigint_handler())
    {
        fprintf(stderr, "Failed to install SIGINT handler.\n");
        return EXIT_FAILURE;
    }

    input_orders_init(&input_orders);
    if(!read_input_orders(options.input_path, &input_orders))
    {
        /* Input must exist and be readable. */
        fprintf(stderr, "Input file cannot be read: %s\n", options.input_path);
        print_usage(argv[0]);
        input_orders_destroy(&input_orders);
        return EXIT_FAILURE;
    }

    if(!cargo_context_init(&context, options.num_couriers))
    {
        fprintf(stderr, "Failed to initialize cargo context.\n");
        input_orders_destroy(&input_orders);
        return EXIT_FAILURE;
    }

    threads = calloc((size_t)options.num_couriers, sizeof(pthread_t));
    courier_args = calloc((size_t)options.num_couriers, sizeof(courier_args_t));

    if(threads == NULL || courier_args == NULL)
    {
        /* If this fails, there is no point to continue. */
        fprintf(stderr, "Failed to allocate courier thread data.\n");
        goto cleanup;
    }

    log_shift_start(&context.logger, options.num_couriers, input_orders.count);

    if(!queue_all_orders(&context, &input_orders))
    {
        goto cleanup;
    }

    started_count = start_couriers(&context, threads, courier_args);
    if(started_count != options.num_couriers)
    {
        /* Stop already created couriers when one create call fails. */
        pthread_mutex_lock(&context.queue_mutex);
        context.shutdown_requested = 1;
        pthread_cond_broadcast(&context.queue_cond);
        pthread_mutex_unlock(&context.queue_mutex);
        join_couriers(threads, started_count);
        goto cleanup;
    }

    wait_until_shift_finishes_or_sigint(&context);

    if(!handle_sigint_shutdown(&context))
    {
        pthread_mutex_lock(&context.queue_mutex);
        context.shutdown_requested = 1;
        pthread_cond_broadcast(&context.queue_cond);
        pthread_mutex_unlock(&context.queue_mutex);
    }

    join_couriers(threads, started_count);
    log_shift_end(&context.logger,
                  &context.completed_orders,
                  &context.cancelled_orders,
                  &context.total_delivery_time);

    if(!write_stats_file(options.stats_path, &context))
    {
        fprintf(stderr, "Failed to write statistics file: %s\n", options.stats_path);
        goto cleanup;
    }

    log_shutdown_complete(&context.logger);
    exit_code = EXIT_SUCCESS;

cleanup:
    free(courier_args);
    free(threads);
    cargo_context_destroy(&context);
    input_orders_destroy(&input_orders);
    return exit_code;
}
