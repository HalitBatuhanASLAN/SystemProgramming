#ifndef AGGREGATOR_H
#define AGGREGATOR_H

/* =============================================================================
 * aggregator.h - Aggregator process interface
 * =============================================================================
 *
 * The Aggregator is the final stage of the pipeline. After every
 * Analyzer has published its level_result_t into Region C, the
 * Aggregator:
 *
 *   1. Waits for all four levels (cond_timedwait + sem_timedwait).
 *   2. Drains Region D in parallel (a dedicated thread) to compute
 *      HIGH_PRIORITY_SCORE.
 *   3. Sorts level results by total_weighted_score DESC.
 *   4. Writes a human-readable .txt file (right-aligned columns,
 *      two-space separators, one decimal place) and a binary .bin file
 *      (with a magic number 0xC5E3440B) atomically (write to .tmp then
 *      rename).
 * ============================================================================= */

#include "shm.h"

typedef struct
{
    shm_region_c_t*  region_c;       /* per-level results              */
    shm_region_d_t*  region_d;       /* high-priority shadow buffer    */

    char**           keywords;
    int              n_keywords;
    int              n_files;        /* for the FILES: header line     */
    int              n_workers;      /* per-thread output expects this */
    int              timeout_sec;    /* T - cond_timedwait duration    */

    const char*      output_path;    /* -o path (.txt)                 */
    const char*      binary_path;    /* -O path (.bin)                 */
    const char*      filter_path;    /* echoed in FILTER_FILE: header  */
} aggregator_arg_t;

/* Entry point: called by the child after fork. Never returns (exits).      */
void aggregator_process_main(aggregator_arg_t* arg);

#endif /* AGGREGATOR_H */