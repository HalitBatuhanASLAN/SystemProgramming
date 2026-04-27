#ifndef ANALYZER_H
#define ANALYZER_H

/* =============================================================================
 * analyzer.h - Analyzer process interface
 * =============================================================================
 *
 * One Analyzer process is forked per log level (4 total: ERROR, WARN,
 * INFO, DEBUG). Each Analyzer:
 *
 *   - reads its level's Region B until EOF;
 *   - splits the work across W worker threads (configurable via -w);
 *   - per worker, counts overlapping keyword matches, multiplies by the
 *     level weight, and accumulates per-keyword + per-thread scores;
 *   - synchronizes worker completion with a pthread_barrier;
 *   - elects a "reporting thread" (the one with the lowest TID) which
 *     publishes the level's level_result_t into Region C and signals the
 *     Aggregator via result_cond + sem_post.
 *
 * Per-thread accounting uses pthread_key_t + a TLS destructor: each
 * worker stores its private scores in TLS, and on exit the destructor
 * flushes them into Region C under the result_mutex. This is the
 * mechanism mandated by the assignment.
 * ============================================================================= */

#include "shm.h"

typedef struct
{
    int              level_idx;        /* 0=ERROR..3=DEBUG                 */
    int              n_workers;        /* W - worker thread count          */
    shm_region_b_t*  region_b;         /* this level's input buffer        */
    shm_region_c_t*  region_c;         /* result publication area          */
    char**           keywords;         /* parsed keyword list              */
    int              n_keywords;
} analyzer_arg_t;

/* =============================================================================
 * count_overlapping - count overlapping occurrences of a substring.
 *
 * Example: count_overlapping("aaaa", "aa") returns 3, not 2, because
 *          a sliding-window matcher finds matches at indices 0, 1, 2.
 *
 * Used by Analyzer (per-entry scoring) AND by the Aggregator's Region D
 * drain thread (HIGH_PRIORITY_SCORE), so it lives in analyzer.c with a
 * public declaration here.
 * ============================================================================= */
int count_overlapping(const char* text, const char* keyword);

/* Entry point: called by the child after fork. Never returns (exits).      */
void analyzer_process_main(analyzer_arg_t* arg);

#endif /* ANALYZER_H */