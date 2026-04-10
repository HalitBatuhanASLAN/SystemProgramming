/*
 * ============================================================
 * FILE: word_carrier_process.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Implements the word-carrier process main loop.
 *   Word-carriers admit words from the input list into the
 *   system one at a time, subject to floor capacity limits.
 *
 * KEY DESIGN DECISIONS:
 *
 *   Round-robin selection
 *     All word-carriers share a single round_robin_index counter
 *     protected by round_robin_mutex.  This guarantees that the
 *     same word is never claimed by two carriers simultaneously
 *     and that all words are visited fairly.
 *
 *   All-or-nothing admission
 *     A word may be admitted ONLY if BOTH its arrival floor AND
 *     its sorting floor have capacity.  To check both atomically
 *     we hold both floor locks at the same time.
 *
 *   Deadlock prevention
 *     When two floor locks must be held together we always acquire
 *     them in ascending floor-number order (lower-numbered floor
 *     first).  This breaks any potential circular wait.
 *
 *   No busy-waiting
 *     If admission fails or no unclaimed word is found, the
 *     process sleeps on state_cond with a 100 ms timeout instead
 *     of spinning.  It wakes up when any state change occurs
 *     (e.g., a word completes and frees floor capacity).
 * ============================================================
 */

#include "word_carrier_process.h"
#include "utils.h"

/* ============================================================
 * word_carrier_run
 * ============================================================ */
void word_carrier_run(SharedData *data, int floor_id, int carrier_id) {

    while (data->system_running) {
        int word_idx = -1;
        int found    = 0;

        /* ── PHASE 1: Round-robin word selection ─────────────── */
        /*
         * Acquire the global round-robin lock so that no two
         * word-carriers can pick the same word simultaneously.
         */
        pthread_mutex_lock(&data->round_robin_mutex);

        /* Check whether every word has already been admitted. */
        int all_admitted = 1;
        for (int i = 0; i < data->total_words; i++) {
            if (!data->words[i].admitted) {
                all_admitted = 0;
                break;
            }
        }
        if (all_admitted) {
            /*
             * All words are in the system.  Signal the other
             * processes and let this carrier exit gracefully.
             */
            data->all_words_admitted = 1;
            pthread_mutex_unlock(&data->round_robin_mutex);
            break;
        }

        /*
         * Scan words[] starting at round_robin_index, wrapping
         * around, until we find a word that is neither claimed
         * nor already admitted.
         */
        for (int attempt = 0; attempt < data->total_words; attempt++) {
            int idx = (data->round_robin_index + attempt) % data->total_words;

            if (!data->words[idx].claimed && !data->words[idx].admitted) {
                word_idx = idx;

                /*
                 * Mark as claimed WHILE holding the round-robin lock
                 * so no other carrier can pick this word in the same
                 * scan cycle.
                 */
                data->words[idx].claimed = 1;

                /* Advance index so the next scan starts after this word. */
                data->round_robin_index = (idx + 1) % data->total_words;
                found = 1;
                break;
            }
        }

        pthread_mutex_unlock(&data->round_robin_mutex);

        /* ── No suitable word found this cycle ───────────────── */
        if (!found) {
            /*
             * Sleep until a state change (word completion frees a
             * floor slot, or another word gets admitted).
             * The 100 ms timeout prevents permanent blocking if a
             * broadcast is missed for any reason.
             */
            pthread_mutex_lock(&data->state_mutex);
            if (data->system_running && !data->all_words_admitted) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100 ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->state_cond,
                                       &data->state_mutex, &ts);
            }
            pthread_mutex_unlock(&data->state_mutex);
            continue;
        }

        /* ── PHASE 2: Atomic capacity check ──────────────────── */
        WordInfo *word         = &data->words[word_idx];
        int       arrival_floor = floor_id;
        int       sorting_floor = word->sorting_floor;

        /*
         * Deadlock prevention: always lock the floor with the
         * smaller floor number first when two locks are needed.
         * If both floors are the same, only one lock is required.
         */
        int lock_first  = (arrival_floor <= sorting_floor) ? arrival_floor : sorting_floor;
        int lock_second = (arrival_floor <= sorting_floor) ? sorting_floor  : arrival_floor;

        pthread_mutex_lock(&data->floors[lock_first].floor_mutex);
        if (lock_first != lock_second) {
            pthread_mutex_lock(&data->floors[lock_second].floor_mutex);
        }

        /*
         * Check capacity for BOTH floors while holding BOTH locks.
         * This is the "all-or-nothing" guarantee: we never reserve
         * capacity on only one floor.
         */
        int arrival_ok = (data->floors[arrival_floor].active_word_count
                          < data->config.max_words_per_floor);
        int sorting_ok = (data->floors[sorting_floor].active_word_count
                          < data->config.max_words_per_floor);

        if (arrival_ok && sorting_ok) {
            /* ── Both floors have room: ADMIT the word ────────── */

            /*
             * Increment active_word_count on BOTH floors.
             * The arrival floor counts it because characters will
             * be picked up from there.
             * The sorting floor counts it because sorting resources
             * will be reserved there.
             * If both floors are the same, only count it once.
             */
            data->floors[arrival_floor].active_word_count++;
            if (arrival_floor != sorting_floor) {
                data->floors[sorting_floor].active_word_count++;
            }

            /* Release floor locks before touching word fields. */
            if (lock_first != lock_second) {
                pthread_mutex_unlock(&data->floors[lock_second].floor_mutex);
            }
            pthread_mutex_unlock(&data->floors[lock_first].floor_mutex);

            /* Update the word's state in shared memory. */
            pthread_mutex_lock(&word->word_mutex);
            word->admitted      = 1;
            word->arrival_floor = arrival_floor;

            /*
             * Set src_floor in every CharTask now that we know
             * where the word physically arrives.
             */
            for (int i = 0; i < word->num_char_tasks; i++) {
                word->char_tasks[i].src_floor = arrival_floor;
            }
            pthread_mutex_unlock(&word->word_mutex);

            /* Update per-carrier admission statistics. */
            pthread_mutex_lock(&data->stats_mutex);
            data->word_carrier_admissions[carrier_id]++;
            pthread_mutex_unlock(&data->stats_mutex);

            /*
             * Broadcast on the arrival floor so that letter-carrier
             * processes sleeping there wake up and start picking
             * up characters immediately.
             */
            pthread_mutex_lock(&data->floors[arrival_floor].floor_mutex);
            pthread_cond_broadcast(&data->floors[arrival_floor].floor_cond);
            pthread_mutex_unlock(&data->floors[arrival_floor].floor_mutex);

            /* Also broadcast on the global state cond. */
            pthread_mutex_lock(&data->state_mutex);
            pthread_cond_broadcast(&data->state_cond);
            pthread_mutex_unlock(&data->state_mutex);

            log_msg("Word-carrier-process_%d claimed word %d",
                    carrier_id, word->word_id);
            log_msg("Word %d admitted to floor %d (sorting floor: %d)",
                    word->word_id, arrival_floor, sorting_floor);

        } else {
            /* ── At least one floor is full: REJECT ──────────── */

            /* Release the floor locks without changing any counts. */
            if (lock_first != lock_second) {
                pthread_mutex_unlock(&data->floors[lock_second].floor_mutex);
            }
            pthread_mutex_unlock(&data->floors[lock_first].floor_mutex);

            /*
             * Reset the claimed flag so this word is available again
             * for the next scan by any word-carrier.
             * Must re-acquire round_robin_mutex to modify claimed safely.
             */
            pthread_mutex_lock(&data->round_robin_mutex);
            word->claimed = 0;
            pthread_mutex_unlock(&data->round_robin_mutex);

            /* Record this failed attempt in the global retry counter. */
            pthread_mutex_lock(&data->stats_mutex);
            data->total_retries++;
            pthread_mutex_unlock(&data->stats_mutex);

            /*
             * Sleep briefly before retrying.  Waiting on state_cond
             * means we wake up as soon as any word completes (freeing
             * a floor slot) rather than spinning for the full 100 ms.
             */
            pthread_mutex_lock(&data->state_mutex);
            if (data->system_running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100 ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->state_cond,
                                       &data->state_mutex, &ts);
            }
            pthread_mutex_unlock(&data->state_mutex);
        }
    }
}
