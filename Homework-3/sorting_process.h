/*
 * PURPOSE:
 * Public interface for the sorting process module.
 * ROLE IN THE SYSTEM:
 * Sorting processes run on a fixed floor and continuously scan words whose sorting_floor matches their assigned floor. As letter-carriers deliver characters into a word's sorting_area[], the sorting process rearranges them until the word is fully reconstructed (every fixed[i] == 1).
 * SORTING RULES (from the assignment):
 * For each slot i in sorting_area[]:
 * Case 1 – Empty (occupied[i]==0)
 * → Skip; nothing to do.
 * Case 2 – Fixed (fixed[i]==1)
 * → Skip; this position is locked in place.
 * Case 3 – Correct position (sorting_area[i] == word[i])
 * → Mark as fixed (fixed[i]=1).
 * Case 4 – Wrong position
 * → Find the target index t where word[t] == char.
 * 4a. target empty            → MOVE char to t.
 * 4b. target occupied & !fixed → SWAP i and t.
 * 4c. target fixed             → skip (do nothing).
 *
 * CONCURRENCY:
 * Multiple sorting processes may exist on the same floor, but only ONE may operate on a given word at a time.  This is enforced by pthread_mutex_trylock(&word->word_mutex): a sorter that cannot immediately acquire the lock simply skips that word and checks it again on the next pass.
 */

#ifndef SORTING_PROCESS_H
#define SORTING_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * sorting_process_run – main loop executed by a sorting process.
 * Runs until data->system_running becomes 0.
 */
void sorting_process_run(SharedData *data, int floor_id, int sorter_id);

#endif /* SORTING_PROCESS_H */