#ifndef DISPATCHER_H
#define DISPATCHER_H

/* =============================================================================
 * dispatcher.h - Dispatcher process interface
 * =============================================================================
 *
 * The Dispatcher is the central router of the pipeline. It pops entries
 * from Region A (filled by Readers) and routes each entry to:
 *
 *   1. Region B[entry.level] - the per-level buffer that the matching
 *      Analyzer drains;
 *   2. Region D - if the entry's source is in the priority filter list,
 *      a copy is also pushed here for the Aggregator's HIGH_PRIORITY_SCORE
 *      computation.
 *
 * EOF handling:
 *   - Each Reader pushes one EOF marker per level into Region A.
 *   - The Dispatcher tracks how many EOFs it has seen for each level.
 *   - Once eof_received[level] == n_readers, the Dispatcher writes a
 *     single EOF flag into Region B[level] (eof_posted = 1) so Analyzer
 *     workers can stop blocking.
 *   - When Region A signals "all EOFs consumed and buffer empty", the
 *     Dispatcher exits and sets dispatcher_done in Region D so the
 *     Aggregator's drain thread can stop too.
 * ============================================================================= */

#include "shm.h"

typedef struct
{
    shm_region_a_t*  region_a;                   /* input ring buffer       */
    shm_region_b_t*  region_b[LEVEL_COUNT];      /* one per level           */
    shm_region_c_t*  region_c;                   /* unused here, kept for   */
                                                 /* symmetry / future use   */
    shm_region_d_t*  region_d;                   /* high-priority shadow    */

    char**           priority_sources;           /* parsed from filter file */
    int              n_priority_sources;

    int              timeout_sec;                /* T - cond_timedwait time */
    int              n_readers;                  /* expected EOFs/level     */
} dispatcher_arg_t;

/* Entry point: called by the child after fork. Never returns (exits).      */
void dispatcher_process_main(dispatcher_arg_t* arg);

#endif /* DISPATCHER_H */