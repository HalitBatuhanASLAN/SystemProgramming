/*
 * ============================================================
 * FILE: letter_carrier_process.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the letter-carrier process module.
 *
 * ROLE IN THE SYSTEM:
 *   Letter-carrier processes are the "couriers" of the system.
 *   They move individual characters from the word's arrival
 *   floor to its sorting floor, using the delivery elevator
 *   whenever the source and destination floors differ.
 *
 *   Unlike word-carrier processes (which never leave their
 *   assigned floor), a letter-carrier is MOBILE: after
 *   delivering a character it stays on the destination floor
 *   and searches for new work there.  If no work is available
 *   on the current floor it uses the REPOSITION elevator to
 *   move to a random floor where work might be waiting.
 *
 * LIFECYCLE OF A LETTER-CARRIER:
 *   1. Start on initial_floor.
 *   2. Find an unclaimed CharTask on the current floor.
 *   3a. Same-floor delivery → place character directly.
 *   3b. Different floor    → enqueue delivery elevator request,
 *                            wait for served==1, move to dest,
 *                            place character.
 *   4. Stay on the destination floor; go to step 2.
 *   5. No task found → request reposition elevator,
 *                      move to random floor, go to step 2.
 *   6. Exit when data->system_running becomes 0.
 * ============================================================
 */

#ifndef LETTER_CARRIER_PROCESS_H
#define LETTER_CARRIER_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * letter_carrier_run – main loop executed by a letter-carrier process.
 *
 * Parameters:
 *   data          – pointer to the shared-memory region.
 *   initial_floor – the floor this carrier starts on.
 *   carrier_id    – unique numeric ID for logging and statistics.
 */
void letter_carrier_run(SharedData *data, int initial_floor, int carrier_id);

#endif /* LETTER_CARRIER_PROCESS_H */
