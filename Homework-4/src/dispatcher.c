#include "dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_priority_source(const char* source, char** list, int n) {
    for (int i = 0; i < n; i++)
        if (strcmp(source, list[i]) == 0) return 1;
    return 0;
}

static void signal_eof_to_b(shm_region_b_t* b) {
    pthread_mutex_lock(&b->level_mutex);
    b->eof_posted = 1;
    pthread_cond_broadcast(&b->not_empty_b);
    pthread_mutex_unlock(&b->level_mutex);
}

void dispatcher_process_main(dispatcher_arg_t* a) {
    printf("[PID:%d] Dispatcher started.\n", getpid());
    fflush(stdout);

    int eof_received[LEVEL_COUNT]  = {0};
    int eof_forwarded[LEVEL_COUNT] = {0};
    long routed = 0, priority_hits = 0;

    while (1) {
        log_entry_t entry;
        int got = shm_a_pop_timed(a->region_a, &entry, a->timeout_sec);
        if (!got) break;

        if (entry.is_eof) {
            eof_received[entry.level]++;
            if (eof_received[entry.level] >= a->n_readers
                && !eof_forwarded[entry.level]) {
                signal_eof_to_b(a->region_b[entry.level]);
                eof_forwarded[entry.level] = 1;
            }
            int all_fwd = 1;
            for (int i = 0; i < LEVEL_COUNT; i++)
                if (!eof_forwarded[i]) { all_fwd = 0; break; }
            if (all_fwd) break;
            continue;
        }

        shm_b_push(a->region_b[entry.level], &entry);
        routed++;

        if (is_priority_source(entry.source, a->priority_sources,
                               a->n_priority_sources)) {
            printf("[PID:%d] High-priority: YES (source: %s, level: %s)\n",
                   getpid(), entry.source, LEVEL_NAMES[entry.level]);
            fflush(stdout);
            shm_d_push(a->region_d, &entry);
            priority_hits++;
        }
    }

    for (int lvl = 0; lvl < LEVEL_COUNT; lvl++)
        if (!eof_forwarded[lvl])
            signal_eof_to_b(a->region_b[lvl]);

    pthread_mutex_lock(&a->region_d->priority_mutex);
    a->region_d->dispatcher_done = 1;
    pthread_cond_broadcast(&a->region_d->not_empty_d);
    pthread_mutex_unlock(&a->region_d->priority_mutex);

    printf("[PID:%d] All EOF markers forwarded. Routed=%ld, Priority=%ld. Exiting.\n",
           getpid(), routed, priority_hits);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
