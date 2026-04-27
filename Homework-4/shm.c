/* shm.c - Shared memory region creation, destruction, and ring-buffer ops
 * Implements the four shared regions declared in shm.h:
 *   - allocates them with mmap(MAP_SHARED|MAP_ANONYMOUS) so every child forked AFTER allocation sees the exact same bytes;
 *   - initializes the embedded mutexes/conds with PTHREAD_PROCESS_SHARED attributes so they are valid across process boundaries;
 *   - provides classic bounded-buffer push/pop operations protected by those primitives, using condition variables to block when full or empty.
*/

#include "shm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

/* Read-only configuration tables (declared "extern" in shm.h). */
const char* LEVEL_NAMES[LEVEL_COUNT] = {"ERROR", "WARN", "INFO", "DEBUG"};
const int LEVEL_WEIGHTS[LEVEL_COUNT] = {4, 3, 2, 1};

/* Size helpers
 * The structs end with a flexible array member, so total size is sizeof(struct) + capacity * sizeof(log_entry_t). mmap() needs the exact byte count, so we wrap it here once.
*/
size_t shm_region_a_size(int capacity)
{
    return sizeof(shm_region_a_t) + (size_t)capacity * sizeof(log_entry_t);
}

size_t shm_region_b_size(int capacity)
{
    return sizeof(shm_region_b_t) + (size_t)capacity * sizeof(log_entry_t);
}

size_t shm_region_d_size(int capacity)
{
    return sizeof(shm_region_d_t) + (size_t)capacity * sizeof(log_entry_t);
}

/* init_shared_mutex - initialize a process-shared mutex.
 * Without PTHREAD_PROCESS_SHARED, behavior is undefined when a forked child accesses the mutex stored in shared memory. */
static void init_shared_mutex(pthread_mutex_t* m)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if(pthread_mutex_init(m, &a) != 0)
    {
        perror("mutex_init");
        exit(1);
    }
    pthread_mutexattr_destroy(&a);
}

/* init_shared_cond - initialize a process-shared condition variable.
 * Same reasoning as init_shared_mutex.
*/
static void init_shared_cond(pthread_cond_t* c)
{
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if(pthread_cond_init(c, &a) != 0)
    {
        perror("cond_init");
        exit(1);
    }
    pthread_condattr_destroy(&a);
}

/* alloc_shared - allocate sz bytes of zero-initialized shared memory.
 * MAP_ANONYMOUS gives memory not backed by any file. MAP_SHARED tells the kernel that any forked child should see the same physical pages.
 * memset(0) gives a clean baseline so head/tail/count fields start at 0.
*/
static void* alloc_shared(size_t sz)
{
    void* p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(p == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }
    memset(p, 0, sz);
    return p;
}

/* Creation functions: mmap + initialize sync primitives + set capacity. */
shm_region_a_t* shm_create_region_a(int capacity)
{
    shm_region_a_t* r = alloc_shared(shm_region_a_size(capacity));
    init_shared_mutex(&r->input_mutex);
    init_shared_cond(&r->not_full_a);
    init_shared_cond(&r->not_empty_a);
    r->capacity = capacity;
    return r;
}

shm_region_b_t* shm_create_region_b(int capacity)
{
    shm_region_b_t* r = alloc_shared(shm_region_b_size(capacity));
    init_shared_mutex(&r->level_mutex);
    init_shared_cond(&r->not_full_b);
    init_shared_cond(&r->not_empty_b);
    r->capacity = capacity;
    return r;
}

shm_region_c_t* shm_create_region_c(void)
{
    shm_region_c_t* r = alloc_shared(sizeof(shm_region_c_t));
    init_shared_mutex(&r->result_mutex);
    init_shared_cond(&r->result_cond);

    /* sem_init's second argument is "pshared": 1 means the semaphore lives in shared memory and is usable across processes. Initial count 0 means sem_wait blocks until a sem_post happens. */
    for(int i = 0; i < LEVEL_COUNT; i++)
    {
        if(sem_init(&r->level_sems[i], 1, 0) != 0)
        {
            perror("sem_init");
            exit(1);
        }
        /* Pre-fill the level name so Analyzer doesn't have to. */
        strncpy(r->results[i].level, LEVEL_NAMES[i], 7);
        r->results[i].level[7] = '\0';
    }
    return r;
}

shm_region_d_t* shm_create_region_d(int capacity)
{
    shm_region_d_t* r = alloc_shared(shm_region_d_size(capacity));
    init_shared_mutex(&r->priority_mutex);
    init_shared_cond(&r->not_full_d);
    init_shared_cond(&r->not_empty_d);
    r->capacity = capacity;
    return r;
}

/* Destruction functions: destroy primitives, then munmap.
 * Order matters: destroy the mutex/cond BEFORE unmapping the memory.
*/
void shm_destroy_region_a(shm_region_a_t* r)
{
    int cap = r->capacity;
    pthread_mutex_destroy(&r->input_mutex);
    pthread_cond_destroy(&r->not_full_a);
    pthread_cond_destroy(&r->not_empty_a);
    munmap(r, shm_region_a_size(cap));
}

void shm_destroy_region_b(shm_region_b_t* r, int capacity)
{
    pthread_mutex_destroy(&r->level_mutex);
    pthread_cond_destroy(&r->not_full_b);
    pthread_cond_destroy(&r->not_empty_b);
    munmap(r, shm_region_b_size(capacity));
}

void shm_destroy_region_c(shm_region_c_t* r)
{
    pthread_mutex_destroy(&r->result_mutex);
    pthread_cond_destroy(&r->result_cond);
    for (int i = 0; i < LEVEL_COUNT; i++)
        sem_destroy(&r->level_sems[i]);
    munmap(r, sizeof(shm_region_c_t));
}

void shm_destroy_region_d(shm_region_d_t* r, int capacity)
{
    pthread_mutex_destroy(&r->priority_mutex);
    pthread_cond_destroy(&r->not_full_d);
    pthread_cond_destroy(&r->not_empty_d);
    munmap(r, shm_region_d_size(capacity));
}

/* Region A - Producer/consumer ring-buffer ops*/

/* shm_a_push:
 * Block (in not_full_a) if the buffer is full, otherwise enqueue the
 * entry and broadcast not_empty_a. EOF entries also tick the per-level
 * EOF counter so consumers can detect "everyone is done". */
void shm_a_push(shm_region_a_t* r, const log_entry_t* e)
{
    pthread_mutex_lock(&r->input_mutex);
    while (r->count == r->capacity)
    {
        pthread_cond_wait(&r->not_full_a, &r->input_mutex);
    }
    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;

    /* Track EOF marker arrivals so consumers can decide when to stop.
     * Only the Reader's parser thread pushes EOFs (one per level per Reader). eof_count_per_level[i] reaches total_readers once every Reader has announced EOF for level i. */
    if (e->is_eof && e->level >= 0 && e->level < LEVEL_COUNT)
        r->eof_count_per_level[e->level]++;

    pthread_cond_broadcast(&r->not_empty_a);
    pthread_mutex_unlock(&r->input_mutex);
}

/* all_eof_done: true when every level's EOF count has reached total_readers. */
static int all_eof_done(shm_region_a_t* r)
{
    for(int i = 0; i < LEVEL_COUNT; i++)
    {
        if(r->eof_count_per_level[i] < r->total_readers)
            return 0;
    }
    return 1;
}

/* shm_a_pop_timed - bounded-time consumer for Region A.
 *
 * Semantics:
 *   - if the buffer is non-empty, dequeue an entry and return 1;
 *   - otherwise, if every Reader has finished (all_eof_done), return 0 so the Dispatcher knows to exit;
 *   - otherwise, block on pthread_cond_timedwait(not_empty_a). When the timeout fires, re-check the predicates and either return or wait again. Spurious wakeups are absorbed by the outer while(1) loop. */
int shm_a_pop_timed(shm_region_a_t* r, log_entry_t* e, int timeout_sec)
{
    pthread_mutex_lock(&r->input_mutex);

    while(1)
    {
        if(r->count > 0)
        {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_a);
            pthread_mutex_unlock(&r->input_mutex);
            return 1;
        }
        if(all_eof_done(r))
        {
            /* Buffer empty AND every EOF has been pushed and consumed.
             * Returning 0 is the agreed signal to the Dispatcher that no more input will ever arrive. */
            pthread_mutex_unlock(&r->input_mutex);
            return 0;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_sec;
        int rc = pthread_cond_timedwait(&r->not_empty_a, &r->input_mutex, &ts);
        if(rc == ETIMEDOUT)
        {
            /* Re-check predicates after timeout. If buffer is still empty AND every Reader is done, we exit; otherwise we wait again. */
            if(r->count == 0 && all_eof_done(r))
            {
                pthread_mutex_unlock(&r->input_mutex);
                return 0;
            }
        }
        /* Spurious wakeup or EINTR: continue the while(1) loop. */
    }
}

/* Region B - per-level ring-buffer ops (Dispatcher -> Analyzer)*/

/* Standard bounded-buffer push: block if full, then enqueue and broadcast. */
void shm_b_push(shm_region_b_t* r, const log_entry_t* e)
{
    pthread_mutex_lock(&r->level_mutex);
    while (r->count == r->capacity)
        pthread_cond_wait(&r->not_full_b, &r->level_mutex);

    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;
    pthread_cond_broadcast(&r->not_empty_b);
    pthread_mutex_unlock(&r->level_mutex);
}

/* shm_b_pop: blocking pop. Returns 0 only when the buffer is empty AND the Dispatcher has set eof_posted. Multiple workers may race on the same Region B; the mutex serializes them so each entry is consumed exactly once. */
int shm_b_pop(shm_region_b_t* r, log_entry_t* e)
{
    pthread_mutex_lock(&r->level_mutex);
    while(1)
    {
        if(r->count > 0)
        {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_b);
            pthread_mutex_unlock(&r->level_mutex);
            return 1;
        }
        if(r->eof_posted)
        {
            pthread_mutex_unlock(&r->level_mutex);
            return 0;
        }
        pthread_cond_wait(&r->not_empty_b, &r->level_mutex);
    }
}

/* Region D - priority shadow ring-buffer ops */

/* shm_d_push: same template as B; called by Dispatcher when an entry's
 * source is in the priority filter list. */
void shm_d_push(shm_region_d_t* r, const log_entry_t* e)
{
    pthread_mutex_lock(&r->priority_mutex);
    while(r->count == r->capacity)
        pthread_cond_wait(&r->not_full_d, &r->priority_mutex);
    
    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;
    pthread_cond_signal(&r->not_empty_d);
    pthread_mutex_unlock(&r->priority_mutex);
}

/* shm_d_pop: returns 0 when buffer empty AND the Dispatcher has signaled dispatcher_done. The Aggregator's drain thread loops on this. */
int shm_d_pop(shm_region_d_t* r, log_entry_t* e)
{
    pthread_mutex_lock(&r->priority_mutex);
    while(1)
    {
        if(r->count > 0)
        {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_d);
            pthread_mutex_unlock(&r->priority_mutex);
            return 1;
        }
        if(r->dispatcher_done)
        {
            pthread_mutex_unlock(&r->priority_mutex);
            return 0;
        }
        pthread_cond_wait(&r->not_empty_d, &r->priority_mutex);
    }
}