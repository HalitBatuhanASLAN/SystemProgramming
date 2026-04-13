/*
 * Public interface for the word-carrier process module.
 * Word-carrier processes act as "gatekeepers" that admit words from the input list into the live system.  They are the first link in the processing pipeline:
 * Input list → [word-carrier] → floor → [letter-carrier]
 * → [sorting process]
 * Each word-carrier process is bound to a single floor (its floor_id never changes) and repeatedly scans the input word list in round-robin order, attempting to admit one word at a time subject to floor capacity constraints.
 */

#ifndef WORD_CARRIER_PROCESS_H
#define WORD_CARRIER_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * word_carrier_run – main loop executed by a word-carrier process.
 * The function runs until either:
 * (a) Every word in the input list has been admitted (data->all_words_admitted becomes 1), or (b) data->system_running becomes 0 (SIGINT / SIGTERM).
 */
void word_carrier_run(SharedData *data, int floor_id, int carrier_id);

#endif