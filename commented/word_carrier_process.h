/*
 * ============================================================
 * FILE: word_carrier_process.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the word-carrier process module.
 *
 * ROLE IN THE SYSTEM:
 *   Word-carrier processes act as "gatekeepers" that admit words
 *   from the input list into the live system.  They are the first
 *   link in the processing pipeline:
 *
 *     Input list → [word-carrier] → floor → [letter-carrier]
 *                                         → [sorting process]
 *
 *   Each word-carrier process is bound to a single floor (its
 *   floor_id never changes) and repeatedly scans the input word
 *   list in round-robin order, attempting to admit one word at a
 *   time subject to floor capacity constraints.
 * ============================================================
 */

#ifndef WORD_CARRIER_PROCESS_H
#define WORD_CARRIER_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * word_carrier_run – main loop executed by a word-carrier process.
 *
 * The function runs until either:
 *   (a) Every word in the input list has been admitted
 *       (data->all_words_admitted becomes 1), or
 *   (b) data->system_running becomes 0 (SIGINT / SIGTERM).
 *
 * Admission algorithm (all-or-nothing):
 *   1. Lock round_robin_mutex.
 *   2. Scan words[] starting from round_robin_index for the next
 *      unclaimed, not-yet-admitted word.
 *   3. Mark it as claimed and advance round_robin_index.
 *   4. Unlock round_robin_mutex.
 *   5. Lock BOTH the arrival floor (floor_id) and the word's
 *      sorting floor, in ascending floor-number order to prevent
 *      deadlocks.
 *   6. Check whether both floors have room (active_word_count <
 *      max_words_per_floor).
 *   7a. Both OK  → increment both counts, release locks, set
 *       admitted=1 and arrival_floor, wake waiting processes.
 *   7b. Either full → release locks, reset claimed=0, increment
 *       total_retries, sleep briefly on state_cond, retry.
 *
 * Parameters:
 *   data        – pointer to the shared-memory region.
 *   floor_id    – the floor this process is permanently assigned to.
 *   carrier_id  – unique numeric ID used for logging and statistics.
 */
void word_carrier_run(SharedData *data, int floor_id, int carrier_id);

#endif /* WORD_CARRIER_PROCESS_H */
