/*
 * dispatcher.c - Dispatcher process implementation
 *
 * Pseudocode:
 * Note that EOF markers do NOT cross Region B as data: the Dispatcher counts them in Region A and converts them into a single eof_posted=1 flag on each Region B[level]. Workers see "buffer empty AND eof_posted" as the signal to exit cleanly.
*/

#include "dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * is_priority_source - linear search of the priority_sources list
 * Returns 1 if the entry's [SOURCE] tag appears in the filter file, 0 otherwise. The list is small (< 100), so linear search is fine. */
static int is_priority_source(const char* source, char** list, int n)
{
    for (int i = 0; i < n; i++)
    {
        if (strcmp(source, list[i]) == 0)
            return 1;
    }
    return 0;
}

/*
 * signal_eof_to_b - mark Region B[level] as exhausted
 * The Dispatcher is the only producer for Region B, so writing eof_posted here is safe under the level_mutex. broadcast() wakes every blocked worker so they all observe eof_posted at once.
*/
static void signal_eof_to_b(shm_region_b_t* b)
{
    pthread_mutex_lock(&b->level_mutex);
    b->eof_posted = 1;
    pthread_cond_broadcast(&b->not_empty_b);
    pthread_mutex_unlock(&b->level_mutex);
}

/*
 * dispatcher_process_main - main routing loop
*/
void dispatcher_process_main(dispatcher_arg_t* a)
{
    printf("[PID:%d] Dispatcher started.\n", getpid());
    fflush(stdout);

    int  eof_received [LEVEL_COUNT] = {0};
    int  eof_forwarded[LEVEL_COUNT] = {0};
    long routed        = 0;
    long priority_hits = 0;

    /* 
     * Main routing loop
     * Pops entries from Region A (timed wait). When shm_a_pop_timed returns 0, every Reader has finished AND Region A is empty; we exit the while loop.
    */
    while (1)
    {
        log_entry_t entry;
        int         got = shm_a_pop_timed(a->region_a, &entry, a->timeout_sec);
        if (!got)
            break;

        /* EOF marker handling: count them, forward once we've seen all Readers for this level. */
        if (entry.is_eof)
        {
            int lvl = entry.level;
            if (lvl >= 0 && lvl < LEVEL_COUNT)
            {
                eof_received[lvl]++;
                if(eof_received[lvl] >= a->n_readers && !eof_forwarded[lvl])
                {
                    signal_eof_to_b(a->region_b[lvl]);
                    eof_forwarded[lvl] = 1;
                }
            }
            continue;
        }

        /* Normal entry: route to Region B[level]. */
        if (entry.level >= 0 && entry.level < LEVEL_COUNT)
        {
            shm_b_push(a->region_b[entry.level], &entry);
            routed++;

            /* Priority check: also push a copy to Region D. */
            if (is_priority_source(entry.source, a->priority_sources, a->n_priority_sources))
            {
                printf("[PID:%d] Routed entry to %s buffer. "
                       "High-priority: YES (source: %s)\n",
                       getpid(), LEVEL_NAMES[entry.level], entry.source);
                fflush(stdout);
                shm_d_push(a->region_d, &entry);
                priority_hits++;
            }
        }
    }

    /*
     * Fail-safe EOF forwarding
     * If for some reason a level never received its expected EOF count (e.g. an empty file produced no entries but still sent EOFs that we did not all see), forward EOF anyway so workers don't hang. */
    for (int lvl = 0; lvl < LEVEL_COUNT; lvl++)
    {
        if (!eof_forwarded[lvl])
        {
            signal_eof_to_b(a->region_b[lvl]);
            eof_forwarded[lvl] = 1;
        }
    }

    /* Tell Region D that no more pushes will happen, so the Aggregator's drain thread can return out of shm_d_pop. */
    pthread_mutex_lock(&a->region_d->priority_mutex);
    a->region_d->dispatcher_done = 1;
    pthread_cond_broadcast(&a->region_d->not_empty_d);
    pthread_mutex_unlock(&a->region_d->priority_mutex);

    printf("[PID:%d] All EOF markers forwarded to Region B. "
           "Routed=%ld, Priority=%ld. Exiting.\n",
           getpid(), routed, priority_hits);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}