/*
 * ============================================================
 * FILE: shared_memory.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the shared-memory management module.
 *   The two functions declared here are the only entry points
 *   for creating and destroying the single SharedData region
 *   that all processes in the system share.
 *
 * IMPLEMENTATION APPROACH:
 *   Instead of POSIX named shared memory (shm_open / shm_unlink),
 *   the system uses mmap() with MAP_SHARED | MAP_ANONYMOUS.
 *   This means:
 *     - No file-system entry is created (no /dev/shm cleanup needed).
 *     - The mapping is inherited automatically by every child
 *       process created with fork().
 *     - The region is released when the last process calls
 *       munmap() or exits.
 * ============================================================
 */

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "common.h"   /* SharedData, SystemConfig, WordInfo, … */

/*
 * shm_init – allocate and initialise the shared-memory region.
 *
 * Allocates sizeof(SharedData) bytes via mmap(MAP_SHARED |
 * MAP_ANONYMOUS), zeroes the region with memset(), then copies
 * the system configuration and the pre-parsed word list into it.
 * Also initialises all runtime counters, elevator states, and
 * per-floor metadata.
 *
 * NOTE: Synchronisation primitives (mutexes, condition variables,
 * semaphores) are NOT initialised here; that is done separately
 * by sync_init() from mutex_semaphore.c.  This separation keeps
 * each module focused on a single responsibility.
 *
 * Parameters:
 *   config     – fully validated system configuration from parse_args().
 *   words      – array of WordInfo structs parsed from the input file.
 *   word_count – number of valid entries in words[].
 *
 * Returns:
 *   Pointer to the new SharedData region on success.
 *   NULL if mmap() fails (errno is set; message printed to stderr).
 */
SharedData *shm_init(SystemConfig *config, WordInfo *words, int word_count);

/*
 * shm_destroy – release the shared-memory region.
 *
 * Calls munmap() on the SharedData pointer.  Should be called by
 * the parent process only AFTER all child processes have been
 * collected with waitpid() and AFTER sync_destroy() has been
 * called (so no synchronisation primitive is in use when the
 * memory disappears).
 *
 * Parameters:
 *   data – pointer returned by a prior shm_init() call.
 *          Passing NULL or MAP_FAILED is safely ignored.
 */
void shm_destroy(SharedData *data);

#endif /* SHARED_MEMORY_H */
