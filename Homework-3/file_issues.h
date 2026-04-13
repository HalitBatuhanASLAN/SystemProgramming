/*
 * Public interface for the file I/O module.  Provides functions to read the system's input file (list of words to be transported and sorted) and to write the final output file once all sorting is complete.
 */

#ifndef FILE_ISSUES_H
#define FILE_ISSUES_H

#include "common.h"   /* Brings in WordInfo, MAX_WORDS, and standard headers */

/*
 * read_input_file – parse the input text file into a WordInfo array.
 * Performs strict format validation:
 * - Exactly two spaces per line (three fields).
 * - No double spaces.
 * - word_id and sorting_floor must parse as decimal integers.
 * - Word characters must be lowercase ASCII letters only.
 * - No blank lines anywhere in the file.
 *
 * Each admitted word gets its CharTask array populated (one task per character), with src_floor left as -1 until a word-carrier assigns an arrival floor.
 */
int read_input_file(const char *path, WordInfo *words, int max_words);

/*
 * write_output_file – write the final sorted word list to a file.
 * Creates (or overwrites) the file at path and writes one line per word in the format "<word_id> <word> <sorting_floor>\n".
 * Lines are sorted by sorting_floor ascending, then by word_id ascending (ties broken by word_id).
 *
 * The original words[] array is NOT modified; a temporary copy is sorted internally.
 */
int write_output_file(const char *path, WordInfo *words, int count);

#endif /* FILE_ISSUES_H */