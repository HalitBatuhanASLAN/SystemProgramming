/*
 * ============================================================
 * FILE: sorting_process.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Implements the sorting process main loop.
 *
 * ALGORITHM OVERVIEW:
 *   Each iteration of the outer while loop scans every word on
 *   the assigned floor.  For each word it tries to acquire
 *   word_mutex (non-blocking with trylock) and, if successful,
 *   performs one pass of the sorting algorithm.
 *
 *   The sorting pass works slot-by-slot:
 *     1. Skip empty or already-fixed slots.
 *     2. If the character is in the correct position → fix it.
 *     3. Otherwise find the first unfixed index t where the
 *        original word has this character:
 *        a. If t is empty  → move the character there (and fix
 *           if it matches word[t]).
 *        b. If t is occupied but not fixed → swap positions i
 *           and t (both may become fixed after the swap).
 *        c. If t is fixed   → skip (cannot displace a fixed char).
 *
 *   After the pass, if every fixed[i] == 1 the word is marked
 *   completed and the system-wide counters are updated.
 *
 * NO BUSY-WAITING:
 *   If a complete scan of the floor's words produced no progress
 *   (did_work == 0), the process sleeps on floor_cond with a
 *   100 ms timeout until a letter-carrier delivers a new character
 *   (which broadcasts on floor_cond).
 * ============================================================
 */

#include "sorting_process.h"
#include "utils.h"

/* ============================================================
 * sorting_process_run
 * ============================================================ */
void sorting_process_run(SharedData *data, int floor_id, int sorter_id) {

    while (data->system_running) {
        int did_work = 0; /* Set to 1 if any progress was made this pass. */

        /* ── Scan all words whose sorting floor matches ours ── */
        for (int i = 0; i < data->total_words && data->system_running; i++) {
            WordInfo *word = &data->words[i];

            /* Only process words assigned to this sorter's floor. */
            if (word->sorting_floor != floor_id) continue;

            /* Skip words not yet in the system or already done. */
            if (!word->admitted || word->completed) continue;

            /*
             * Use trylock (non-blocking) so we don't stall this
             * sorter waiting for a word that another sorter is
             * currently processing.  We simply move on to the next
             * word and try again later.
             */
            if (pthread_mutex_trylock(&word->word_mutex) != 0) {
                continue;
            }

            /*
             * Re-check completed after acquiring the lock because
             * another sorter may have just finished this word between
             * our initial check and the lock acquisition.
             */
            if (word->completed) {
                pthread_mutex_unlock(&word->word_mutex);
                continue;
            }

            /* ── Quick state snapshot ────────────────────────── */
            int all_fixed    = 1; /* True until a non-fixed slot is found. */
            int has_occupied = 0; /* True if at least one char has arrived. */

            for (int j = 0; j < word->word_len; j++) {
                if (!word->occupied[j] || !word->fixed[j])
                    all_fixed = 0;
                if (word->occupied[j])
                    has_occupied = 1;
            }

            /*
             * If no character has arrived yet there is nothing to
             * sort; skip and wait for letter-carriers.
             */
            if (!has_occupied) {
                pthread_mutex_unlock(&word->word_mutex);
                continue;
            }

            /* ── Sorting pass: iterate over every slot ────────── */
            for (int j = 0; j < word->word_len; j++) {

                /* Case 1: slot is empty → nothing to do. */
                if (!word->occupied[j]) continue;

                /* Case 2: slot is already fixed → nothing to do. */
                if (word->fixed[j]) continue;

                /* Case 3: character is already in the correct position. */
                if (word->sorting_area[j] == word->word[j]) {
                    word->fixed[j] = 1;
                    did_work = 1;
                    log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                            sorter_id, word->sorting_area[j],
                            word->word_id, floor_id);
                    continue;
                }

                /* Case 4: character is in the wrong position. */
                char current_char = word->sorting_area[j];

                /*
                 * Find the first unfixed index t where word[t] equals
                 * current_char (i.e., where this character belongs).
                 * We stop at the first match to avoid unnecessary moves.
                 */
                int target_pos = -1;
                for (int k = 0; k < word->word_len; k++) {
                    if (word->word[k] == current_char && !word->fixed[k]) {
                        target_pos = k;
                        break;
                    }
                }

                /* No valid target found (all matching positions are fixed). */
                if (target_pos == -1) continue;

                /* Edge case: target is the same slot (shouldn't happen
                 * given Case 3 already checked, but handle safely). */
                if (target_pos == j) {
                    if (word->sorting_area[j] == word->word[j]) {
                        word->fixed[j] = 1;
                        did_work = 1;
                        log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                                sorter_id, word->sorting_area[j],
                                word->word_id, floor_id);
                    }
                    continue;
                }

                if (!word->occupied[target_pos]) {
                    /* ── Case 4a: target slot is empty → MOVE ──── */
                    word->sorting_area[target_pos] = current_char;
                    word->occupied[target_pos]     = 1;
                    word->sorting_area[j]          = '\0';
                    word->occupied[j]              = 0;
                    did_work = 1;

                    log_msg("Sorting-process_%d moved char '%c' of word %d to correct index",
                            sorter_id, current_char, word->word_id);

                    /* The moved character might now be in its final place. */
                    if (word->sorting_area[target_pos] == word->word[target_pos]) {
                        word->fixed[target_pos] = 1;
                        log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                                sorter_id, current_char, word->word_id, floor_id);
                    }

                } else if (!word->fixed[target_pos]) {
                    /* ── Case 4b: target occupied & not fixed → SWAP */
                    char temp = word->sorting_area[target_pos];
                    word->sorting_area[target_pos] = current_char;
                    word->sorting_area[j]          = temp;
                    did_work = 1;

                    log_msg("Sorting-process_%d swap performed for word %d",
                            sorter_id, word->word_id);

                    /* After the swap BOTH positions might be correct. */
                    if (word->sorting_area[target_pos] == word->word[target_pos]) {
                        word->fixed[target_pos] = 1;
                        log_msg("Sorting-process_%d fixed one index of word %d",
                                sorter_id, word->word_id);
                    }
                    if (word->sorting_area[j] == word->word[j]) {
                        word->fixed[j] = 1;
                        log_msg("Sorting-process_%d fixed one more position of word %d",
                                sorter_id, word->word_id);
                    }
                }
                /* Case 4c: target_pos is fixed → skip (cannot displace it). */
            }

            /* ── Check for word completion ────────────────────── */
            all_fixed = 1;
            for (int j = 0; j < word->word_len; j++) {
                if (!word->fixed[j]) {
                    all_fixed = 0;
                    break;
                }
            }

            if (all_fixed) {
                /*
                 * All characters are in their correct final positions.
                 * Mark the word as completed and update global state.
                 *
                 * We save the floors to stack variables before unlocking
                 * because we need them after the lock is released.
                 */
                word->completed = 1;
                int saved_arrival = word->arrival_floor;
                int saved_sorting = word->sorting_floor;
                int saved_word_id = word->word_id;

                pthread_mutex_unlock(&word->word_mutex);

                log_msg("Word %d COMPLETED", saved_word_id);

                /* Update global completion counter. */
                pthread_mutex_lock(&data->stats_mutex);
                data->completed_words++;
                data->sorting_process_completions[sorter_id]++;
                pthread_mutex_unlock(&data->stats_mutex);

                /*
                 * Decrement active_word_count on the arrival floor.
                 * This frees a slot so word-carriers waiting for
                 * capacity can admit another word.
                 */
                pthread_mutex_lock(&data->floors[saved_arrival].floor_mutex);
                data->floors[saved_arrival].active_word_count--;
                pthread_cond_broadcast(&data->floors[saved_arrival].floor_cond);
                pthread_mutex_unlock(&data->floors[saved_arrival].floor_mutex);

                /*
                 * Also decrement on the sorting floor if it is
                 * different from the arrival floor (both were
                 * incremented on admission).
                 */
                if (saved_arrival != saved_sorting) {
                    pthread_mutex_lock(&data->floors[saved_sorting].floor_mutex);
                    data->floors[saved_sorting].active_word_count--;
                    pthread_cond_broadcast(&data->floors[saved_sorting].floor_cond);
                    pthread_mutex_unlock(&data->floors[saved_sorting].floor_mutex);
                }

                /* Broadcast on the global state cond so the parent
                 * monitor and word-carriers wake up promptly. */
                pthread_mutex_lock(&data->state_mutex);
                pthread_cond_broadcast(&data->state_cond);
                pthread_mutex_unlock(&data->state_mutex);

                continue; /* Word is done; move to the next word. */
            }

            pthread_mutex_unlock(&word->word_mutex);
        }

        /* ── Sleep if no progress was made this pass ─────────── */
        /*
         * If did_work is still 0 after scanning every word on this
         * floor, there is nothing to sort right now (all characters
         * for pending words are still being transported).  Sleep on
         * floor_cond until a letter-carrier delivers a new character
         * and broadcasts, or until the 100 ms timeout expires.
         */
        if (!did_work) {
            pthread_mutex_lock(&data->floors[floor_id].floor_mutex);
            if (data->system_running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100 ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->floors[floor_id].floor_cond,
                                       &data->floors[floor_id].floor_mutex,
                                       &ts);
            }
            pthread_mutex_unlock(&data->floors[floor_id].floor_mutex);
        }
    }
}
