/*
 * PURPOSE:
 * Public interface for the shared-memory management module.The two functions declared here are the only entry points for creating and destroying the single SharedData region that all processes in the system share.
 * IMPLEMENTATION APPROACH:
 * Instead of POSIX named shared memory (shm_open / shm_unlink), the system uses mmap() with MAP_SHARED | MAP_ANONYMOUS.
 */

#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "common.h"   /* SharedData, SystemConfig, WordInfo, … */

/*
 * shm_init – allocate and initialise the shared-memory region.
 * Allocates sizeof(SharedData) bytes via mmap(MAP_SHARED | MAP_ANONYMOUS), zeroes the region with memset(), then copies the system configuration and the pre-parsed word list into it. Also initialises all runtime counters, elevator states, and per-floor metadata.
 *
 * NOTE: Synchronisation primitives (mutexes, condition variables, semaphores) are NOT initialised here; that is done separately by sync_init() from mutex_semaphore.c.  This separation keeps each module focused on a single responsibility.
 */
SharedData *shm_init(SystemConfig *config, WordInfo *words, int word_count);

/* shm_destroy – release the shared-memory region.*/
void shm_destroy(SharedData *data);

#endif