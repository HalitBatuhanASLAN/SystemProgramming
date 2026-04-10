/*
 * ============================================================
 * FILE: file_issues.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the file I/O module.  Provides
 *   functions to read the system's input file (list of words
 *   to be transported and sorted) and to write the final
 *   output file once all sorting is complete.
 *
 * INPUT FILE FORMAT (per the assignment specification):
 *   - One word per line.
 *   - Each line contains exactly three fields separated by a
 *     single space:  <word_id> <word> <sorting_floor>
 *   - No blank lines, no leading/trailing spaces.
 *   - The word must contain only lowercase English letters.
 *   - After sorting_floor there must be an immediate newline.
 *
 *   Example:
 *     101 apple 2
 *     102 process 0
 *
 * OUTPUT FILE FORMAT:
 *   - Same three-field format as input.
 *   - Lines sorted first by sorting_floor, then by word_id.
 *   - No blank lines.
 * ============================================================
 */

#ifndef FILE_ISSUES_H
#define FILE_ISSUES_H

#include "common.h"   /* WordInfo, MAX_WORDS, standard headers */

/*
 * read_input_file – parse the input text file into a WordInfo array.
 *
 * Performs strict format validation:
 *   - Exactly two spaces per line (three fields).
 *   - No double spaces.
 *   - word_id and sorting_floor must parse as decimal integers.
 *   - Word characters must be lowercase ASCII letters only.
 *   - No blank lines anywhere in the file.
 *
 * Each admitted word gets its CharTask array populated (one task
 * per character), with src_floor left as -1 until a word-carrier
 * assigns an arrival floor.
 *
 * Parameters:
 *   path      – path to the input .txt file.
 *   words     – caller-provided array; filled on success.
 *   max_words – capacity of words[] (usually MAX_WORDS).
 *
 * Returns:
 *   > 0  – number of words successfully parsed.
 *   -1   – on any error (bad format, unreadable file, too many words);
 *           a descriptive message is written to stderr.
 */
int read_input_file(const char *path, WordInfo *words, int max_words);

/*
 * write_output_file – write the final sorted word list to a file.
 *
 * Creates (or overwrites) the file at path and writes one line
 * per word in the format "<word_id> <word> <sorting_floor>\n".
 * Lines are sorted by sorting_floor ascending, then by word_id
 * ascending (ties broken by word_id).
 *
 * The original words[] array is NOT modified; a temporary copy
 * is sorted internally.
 *
 * Parameters:
 *   path  – path to the output .txt file.
 *   words – array of all WordInfo structs (completed or not).
 *   count – number of entries in words[].
 *
 * Returns:
 *    0  on success.
 *   -1  on error (malloc failure or fopen failure);
 *       a descriptive message is written to stderr.
 */
int write_output_file(const char *path, WordInfo *words, int count);

#endif /* FILE_ISSUES_H */
