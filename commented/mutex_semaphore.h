/*
 * ============================================================
 * FILE: mutex_semaphore.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the synchronisation initialisation and
 *   teardown module.
 *
 * WHAT "PROCESS-SHARED" MEANS:
 *   By default, a pthread_mutex_t or pthread_cond_t only works
 *   between threads of the SAME process.  Because this system
 *   uses fork() to create independent processes that all share
 *   the same mmap() region, every primitive must be initialised
 *   with PTHREAD_PROCESS_SHARED so the kernel knows it may be
 *   accessed from multiple distinct process address spaces.
 *
 *   Similarly, semaphores must be initialised with sem_init()
 *   using pshared=1 (non-zero) for the same reason.
 *
 * CALL ORDER IN MAIN:
 *   shm_init()   → allocates the SharedData region.
 *   sync_init()  → initialises all primitives inside it.
 *   fork()…      → creates child processes.
 *   …work…
 *   sync_destroy() → destroys all primitives.
 *   shm_destroy()  → releases the mmap region.
 * ============================================================
 */

#ifndef MUTEX_SEMAPHORE_H
#define MUTEX_SEMAPHORE_H

#include "common.h"   /* SharedData and all standard headers */

/*
 * sync_init – initialise every synchronisation primitive inside
 *             the SharedData region for cross-process use.
 *
 * Primitives initialised (all with PTHREAD_PROCESS_SHARED):
 *   - word_mutex          for every WordInfo in data->words[]
 *   - floor_mutex         for every Floor in data->floors[]
 *   - floor_cond          for every Floor in data->floors[]
 *   - delivery_elevator:  elev_mutex, elev_cond, request_sem
 *   - reposition_elevator: elev_mutex, elev_cond, request_sem
 *   - round_robin_mutex   (global word-selection lock)
 *   - stats_mutex         (global statistics lock)
 *   - state_mutex         (global state-change lock)
 *   - state_cond          (global state-change condition variable)
 *   - children_mutex      (child PID list lock)
 *
 * Must be called AFTER shm_init() and BEFORE any child process
 * is forked, so the primitives exist before they are used.
 *
 * Returns:
 *    0  on success.
 *   -1  if any primitive fails to initialise; a descriptive
 *       message is written to stderr.  The caller should abort.
 */
int sync_init(SharedData *data);

/*
 * sync_destroy – destroy every synchronisation primitive created
 *                by sync_init().
 *
 * Must be called AFTER all child processes have been collected
 * (waitpid) and BEFORE shm_destroy() releases the memory.
 * Destroying a primitive that is still in use is undefined
 * behaviour.
 *
 * Parameters:
 *   data – pointer to the shared-memory region (must not be NULL).
 */
void sync_destroy(SharedData *data);

#endif /* MUTEX_SEMAPHORE_H */
