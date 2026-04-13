/*
 * PURPOSE:
 * Initialises and destroys all synchronisation primitives stored inside the shared-memory region (SharedData).
 * KEY CONCEPT – PROCESS-SHARED PRIMITIVES:
 * Normally pthread_mutex_t and pthread_cond_t only synchronise threads within a single process.  To make them work across fork()-ed processes that share the same mmap() region, each primitive must be created with the PTHREAD_PROCESS_SHARED attribute.  The two static helper functions below handle this boilerplate for mutexes and condition variables.
 * POSIX semaphores are made process-shared by passing pshared=1 to sem_init().
 */
#include "mutex_semaphore.h"

/* init_shared_mutex  (file-scope helper)
 * Creates a single pthread_mutex_t that can be used by multiple independent processes (not just threads).*/
static int init_shared_mutex(pthread_mutex_t *mutex)
{
    pthread_mutexattr_t attr;

    /* Initialise the attribute object itself. */
    if(pthread_mutexattr_init(&attr) != 0)
        return -1;

    /* Mark the mutex as usable across process boundaries. */
    if(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0)
    {
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    /* Create the mutex using the process-shared attribute. */
    int ret = pthread_mutex_init(mutex, &attr);

    /* The attribute object is no longer needed after init. */
    pthread_mutexattr_destroy(&attr);

    return (ret == 0) ? 0 : -1;
}

/* init_shared_cond  (file-scope helper)
 * Creates a single pthread_cond_t that can be signalled and waited on across multiple independent processes.
 * The pattern is identical to init_shared_mutex: create an attribute object, set PTHREAD_PROCESS_SHARED, initialise the cond, destroy the attribute.
*/
static int init_shared_cond(pthread_cond_t *cond)
{
    pthread_condattr_t attr;

    if(pthread_condattr_init(&attr) != 0)
        return -1;

    if(pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0)
    {
        pthread_condattr_destroy(&attr);
        return -1;
    }

    int ret = pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);

    return (ret == 0) ? 0 : -1;
}

/* Initialises EVERY synchronisation primitive in SharedData.
 * Called once by the parent process after shm_init() and before any child processes are forked.
 * Initialisation order (matches the declaration order in common.h for clarity, but order does not matter for correctness because no process uses the primitives yet):
*/
int sync_init(SharedData *data)
{

    /* ── 1. Per-word mutexes ─────────────────────────────────── */
    /*
     * Each word has its own mutex so that multiple sorting processes on the same floor can work on DIFFERENT words simultaneously, but only ONE sorter can modify a given word at a time.
     */
    for(int i = 0;i<data->total_words;i++)
    {
        if(init_shared_mutex(&data->words[i].word_mutex) != 0)
        {
            fprintf(stderr, "Error: Failed to init word mutex %d\n", i);
            return -1;
        }
    }

    /* ── 2. Per-floor mutex and condition variable ───────────── */
    /*
     * floor_mutex protects active_word_count and letter_carrier_count.  floor_cond is broadcast whenever one of those counts changes so that processes waiting for capacity or carriers wake up promptly.
     */
    for(int i = 0;i<data->config.num_floors;i++)
    {
        if(init_shared_mutex(&data->floors[i].floor_mutex) != 0)
        {
            fprintf(stderr, "Error: Failed to init floor mutex %d\n", i);
            return -1;
        }
        if(init_shared_cond(&data->floors[i].floor_cond) != 0)
        {
            fprintf(stderr, "Error: Failed to init floor cond %d\n", i);
            return -1;
        }
    }

    /* ── 3. Delivery elevator synchronisation ───────────────── */
    /*
     * elev_mutex protects the request queue, current floor, direction, and load.
     * elev_cond is used by letter-carrier processes to wait for their delivery request to reach served==1. request_sem is posted by any carrier that enqueues a new request; the elevator process blocks on sem_timedwait() instead of busy-waiting.
     */
    if(init_shared_mutex(&data->delivery_elevator.elev_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init delivery elevator mutex\n");
        return -1;
    }
    if(init_shared_cond(&data->delivery_elevator.elev_cond) != 0)
    {
        fprintf(stderr, "Error: Failed to init delivery elevator cond\n");
        return -1;
    }
    /*
     * sem_init(sem, pshared, value):
     * pshared = 1  → semaphore is shared between processes.
     * value   = 0  → elevator starts blocked (no requests yet).
     */
    if(sem_init(&data->delivery_elevator.request_sem, 1, 0) != 0)
    {
        fprintf(stderr, "Error: Failed to init delivery elevator semaphore\n");
        return -1;
    }

    /* ── 4. Reposition elevator synchronisation ─────────────── */
    /*
     * Identical structure to the delivery elevator but operates independently on a separate request queue.
     */
    if(init_shared_mutex(&data->reposition_elevator.elev_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init reposition elevator mutex\n");
        return -1;
    }
    if(init_shared_cond(&data->reposition_elevator.elev_cond) != 0)
    {
        fprintf(stderr, "Error: Failed to init reposition elevator cond\n");
        return -1;
    }
    if(sem_init(&data->reposition_elevator.request_sem, 1, 0) != 0)
    {
        fprintf(stderr, "Error: Failed to init reposition elevator semaphore\n");
        return -1;
    }

    /* ── 5. Global round-robin mutex ────────────────────────── */
    /*
     * Protects round_robin_index and the claimed flag during atomic word selection by word-carrier processes. Without this lock two carriers could claim the same word.
     */
    if(init_shared_mutex(&data->round_robin_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init round-robin mutex\n");
        return -1;
    }

    /* ── 6. Statistics mutex ────────────────────────────────── */
    /*
     * Protects all counters in SharedData (total_retries, total_chars_transported, per-process arrays, …) so concurrent increments do not produce race conditions.
     */
    if(init_shared_mutex(&data->stats_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init stats mutex\n");
        return -1;
    }

    /* ── 7. Global state mutex and condition variable ────────── */
    /*
     * state_mutex / state_cond form a general-purpose notification channel: whenever any significant state changes (word admitted, character delivered, word completed) a broadcast wakes any process that was sleeping here.
     */
    if(init_shared_mutex(&data->state_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init state mutex\n");
        return -1;
    }
    if(init_shared_cond(&data->state_cond) != 0)
    {
        fprintf(stderr, "Error: Failed to init state condition variable\n");
        return -1;
    }

    /* ── 8. Children list mutex ─────────────────────────────── */
    /*
     * Protects child_pids[] and num_children so that the parent can safely register new PIDs from within the forking loop without data corruption.
     */
    if(init_shared_mutex(&data->children_mutex) != 0)
    {
        fprintf(stderr, "Error: Failed to init children mutex\n");
        return -1;
    }

    return 0; /* All primitives initialised successfully. */
}

/* Destroys every synchronisation primitive in the reverse order they were created.  Must be called after all child processes have exited so no primitive is in use during destruction.
*/
void sync_destroy(SharedData *data)
{

    /* Destroy per-word mutexes. */
    for(int i = 0;i<data->total_words;i++)
        pthread_mutex_destroy(&data->words[i].word_mutex);

    /* Destroy per-floor mutex and condition variable. */
    for(int i = 0;i<data->config.num_floors;i++)
    {
        pthread_mutex_destroy(&data->floors[i].floor_mutex);
        pthread_cond_destroy(&data->floors[i].floor_cond);
    }

    /* Destroy delivery elevator primitives. */
    pthread_mutex_destroy(&data->delivery_elevator.elev_mutex);
    pthread_cond_destroy(&data->delivery_elevator.elev_cond);
    sem_destroy(&data->delivery_elevator.request_sem);

    /* Destroy reposition elevator primitives. */
    pthread_mutex_destroy(&data->reposition_elevator.elev_mutex);
    pthread_cond_destroy(&data->reposition_elevator.elev_cond);
    sem_destroy(&data->reposition_elevator.request_sem);

    /* Destroy global mutexes and condition variable. */
    pthread_mutex_destroy(&data->round_robin_mutex);
    pthread_mutex_destroy(&data->stats_mutex);
    pthread_mutex_destroy(&data->state_mutex);
    pthread_cond_destroy(&data->state_cond);
    pthread_mutex_destroy(&data->children_mutex);
}