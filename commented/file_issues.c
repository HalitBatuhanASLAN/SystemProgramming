/*
 * ============================================================
 * FILE: file_issues.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Reads the input word list and writes the final output file.
 *
 * DESIGN NOTES:
 *   - read_input_file() performs strict line-by-line validation
 *     so malformed input is caught before any process is forked.
 *   - write_output_file() works on a temporary copy so the
 *     shared-memory words[] array is never reordered.
 *   - Both functions use only stdio (fopen/fgets/fprintf) which
 *     is simpler and more portable than raw syscalls for file I/O.
 * ============================================================
 */

#include "file_issues.h"
#include <ctype.h>    /* islower(), isalpha() */

/* ============================================================
 * compare_words  (file-scope helper for qsort)
 * ------------------------------------------------------------
 * Comparison function used by write_output_file() to sort the
 * output array.
 *
 * Primary key:   sorting_floor  (ascending)
 * Secondary key: word_id        (ascending, breaks ties)
 *
 * This matches the output format required by the assignment:
 *   rows ordered first by which floor they sort on, then by
 *   the word's unique numeric identifier.
 * ============================================================ */
static int compare_words(const void *a, const void *b) {
    const WordInfo *wa = (const WordInfo *)a;
    const WordInfo *wb = (const WordInfo *)b;

    /* If sorting floors differ, order by floor number. */
    if (wa->sorting_floor != wb->sorting_floor)
        return wa->sorting_floor - wb->sorting_floor;

    /* Same floor → order by word_id. */
    return wa->word_id - wb->word_id;
}

/* ============================================================
 * read_input_file
 * ------------------------------------------------------------
 * Parses the input file line by line into the words[] array.
 *
 * Validation performed on each line:
 *   1. Not blank / empty.
 *   2. Contains exactly two single spaces (three fields).
 *   3. No leading or trailing spaces.
 *   4. word_id and sorting_floor parse cleanly as integers
 *      (no extra characters, no overflow).
 *   5. The word contains only lowercase English letters.
 *
 * After validation, each WordInfo is zero-filled and then
 * populated including its CharTask array (one entry per letter).
 * ============================================================ */
int read_input_file(const char *path, WordInfo *words, int max_words) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n",
                path, strerror(errno));
        return -1;
    }

    int  count   = 0;
    char line[512];
    int  line_no = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        /* Working variables declared at block scope for C89 compat. */
        int    wid, sf;
        char   word_buf[MAX_WORD_LEN];
        char  *first_space;
        char  *second_space;
        char  *newline_pos;
        char   id_buf[64];
        char   sf_buf[64];
        char  *endptr;
        size_t len;

        line_no++;
        len = strlen(line);

        /* ── Reject blank/empty lines ────────────────────────── */
        if (len == 0 || line[0] == '\n') {
            fprintf(stderr,
                    "Error: Input file contains blank line at line %d\n",
                    line_no);
            fclose(fp);
            return -1;
        }

        /*
         * If the line did not end with '\n' and we are NOT at EOF,
         * the line is too long for our buffer (or the file uses
         * non-standard line endings).
         */
        if (line[len - 1] != '\n' && !feof(fp)) {
            fprintf(stderr,
                    "Error: Line %d is too long or missing newline\n",
                    line_no);
            fclose(fp);
            return -1;
        }

        /*
         * If the last line of the file has no trailing newline
         * (which is valid on some systems), synthesise one so the
         * rest of the parsing logic can assume '\n' always exists.
         */
        if (line[len - 1] != '\n' && feof(fp)) {
            if (len + 1 >= sizeof(line)) {
                fprintf(stderr, "Error: Line %d is too long\n", line_no);
                fclose(fp);
                return -1;
            }
            line[len]     = '\n';
            line[len + 1] = '\0';
            len++;
        }

        /* ── Locate the two mandatory space separators ────────── */
        first_space  = strchr(line, ' ');
        second_space = first_space ? strchr(first_space + 1, ' ') : NULL;
        newline_pos  = strchr(line, '\n');

        if (!first_space || !second_space) {
            fprintf(stderr, "Error: Invalid spacing at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        /*
         * Reject double spaces (the spec requires single spaces only).
         * Also reject a leading space (first_space == line).
         * Also reject a third space after the sorting_floor field.
         */
        if (first_space[1] == ' ' || second_space[1] == ' ') {
            fprintf(stderr,
                    "Error: Input line %d must use single spaces only\n",
                    line_no);
            fclose(fp);
            return -1;
        }
        if (strchr(second_space + 1, ' ') != NULL) {
            fprintf(stderr,
                    "Error: Input line %d must contain exactly three fields\n",
                    line_no);
            fclose(fp);
            return -1;
        }
        if (line[0] == ' ' || first_space == line ||
            second_space == first_space + 1) {
            fprintf(stderr,
                    "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        /* ── Handle Windows-style CRLF line endings ───────────── */
        if (len >= 2 && line[len - 2] == '\r') {
            line[len - 2] = '\n';
            line[len - 1] = '\0';
            len--;
            newline_pos = strchr(line, '\n');
        }

        /* Sorting_floor field must not be empty (newline right after second space). */
        if (!newline_pos || newline_pos == second_space + 1) {
            fprintf(stderr,
                    "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        /* ── Extract the three raw field strings ─────────────── */
        size_t id_len   = (size_t)(first_space  - line);
        size_t word_len = (size_t)(second_space - first_space  - 1);
        size_t sf_len   = (size_t)(newline_pos  - second_space - 1);

        if (id_len   == 0 || id_len   >= sizeof(id_buf)   ||
            word_len == 0 || word_len >= sizeof(word_buf)  ||
            sf_len   == 0 || sf_len   >= sizeof(sf_buf)) {
            fprintf(stderr,
                    "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        memcpy(id_buf,   line,             id_len);   id_buf[id_len]     = '\0';
        memcpy(word_buf, first_space  + 1, word_len); word_buf[word_len] = '\0';
        memcpy(sf_buf,   second_space + 1, sf_len);   sf_buf[sf_len]     = '\0';

        /* ── Parse word_id as a decimal integer ─────────────── */
        /*
         * strtol() with endptr lets us detect trailing garbage
         * (e.g., "101abc") that atoi() would silently accept.
         */
        errno = 0;
        wid   = (int)strtol(id_buf, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            fprintf(stderr, "Error: Invalid word_id at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        /* ── Parse sorting_floor ─────────────────────────────── */
        errno = 0;
        sf    = (int)strtol(sf_buf, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            fprintf(stderr,
                    "Error: Invalid sorting_floor at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        /* ── Validate word characters ────────────────────────── */
        /*
         * The assignment requires all-lowercase English letters only.
         * Digits, punctuation, uppercase, or non-ASCII are rejected.
         */
        for (int i = 0; word_buf[i] != '\0'; i++) {
            if (!islower((unsigned char)word_buf[i]) ||
                !isalpha((unsigned char)word_buf[i])) {
                fprintf(stderr,
                        "Error: Word '%s' at line %d must contain "
                        "lowercase English letters only\n",
                        word_buf, line_no);
                fclose(fp);
                return -1;
            }
        }

        /* ── Capacity check ──────────────────────────────────── */
        if (count >= max_words) {
            fprintf(stderr,
                    "Error: Too many words in input (max %d)\n", max_words);
            fclose(fp);
            return -1;
        }

        /* ── Populate WordInfo ────────────────────────────────── */
        WordInfo *w = &words[count];
        memset(w, 0, sizeof(WordInfo));

        w->word_id      = wid;
        strncpy(w->word, word_buf, MAX_WORD_LEN - 1);
        w->word[MAX_WORD_LEN - 1] = '\0';
        w->word_len     = (int)strlen(w->word);
        w->sorting_floor = sf;
        w->arrival_floor = -1;  /* Not yet assigned to any floor. */
        w->claimed      = 0;    /* Not yet claimed by a word-carrier. */
        w->admitted     = 0;    /* Not yet admitted into the system.  */
        w->completed    = 0;    /* Not yet sorted.                    */

        /* Zero the sorting state arrays. */
        memset(w->sorting_area, 0, MAX_WORD_LEN);
        memset(w->occupied,     0, sizeof(w->occupied));
        memset(w->fixed,        0, sizeof(w->fixed));

        /* ── Generate one CharTask per character ─────────────── */
        /*
         * Each character of the word becomes an independent task
         * that a letter-carrier process will claim and deliver.
         * src_floor is set to -1 here; it will be updated to the
         * actual arrival_floor when a word-carrier admits the word.
         */
        w->num_char_tasks = w->word_len;
        for (int i = 0; i < w->word_len; i++) {
            w->char_tasks[i].word_id        = wid;
            w->char_tasks[i].character      = w->word[i];
            w->char_tasks[i].original_index = i;
            w->char_tasks[i].src_floor      = -1;   /* Set on admission. */
            w->char_tasks[i].dest_floor     = sf;
            w->char_tasks[i].claimed        = 0;
            w->char_tasks[i].delivered      = 0;
        }

        count++;
    }

    fclose(fp);

    if (count == 0) {
        fprintf(stderr, "Error: No valid words found in input file\n");
        return -1;
    }

    return count;
}

/* ============================================================
 * write_output_file
 * ------------------------------------------------------------
 * Writes the final word list to the output file.
 *
 * Uses a temporary malloc'd copy of the words[] array so the
 * original shared-memory data is never reordered (other
 * processes might still be referencing it during shutdown).
 *
 * The copy is sorted by (sorting_floor, word_id) using qsort()
 * with compare_words() as the comparator.
 * ============================================================ */
int write_output_file(const char *path, WordInfo *words, int count) {

    /* Allocate a temporary copy to avoid mutating shared memory. */
    WordInfo *sorted = malloc(sizeof(WordInfo) * count);
    if (!sorted) {
        fprintf(stderr, "Error: malloc failed for output sorting\n");
        return -1;
    }
    memcpy(sorted, words, sizeof(WordInfo) * count);

    /*
     * qsort() is not stable, but the two-key comparator in
     * compare_words() provides a total order so the result is
     * deterministic regardless of the initial array order.
     */
    qsort(sorted, count, sizeof(WordInfo), compare_words);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open output file '%s': %s\n",
                path, strerror(errno));
        free(sorted);
        return -1;
    }

    /* Write one line per word: "<word_id> <word> <sorting_floor>\n" */
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %s %d\n",
                sorted[i].word_id,
                sorted[i].word,
                sorted[i].sorting_floor);
    }

    fclose(fp);
    free(sorted);
    return 0;
}
