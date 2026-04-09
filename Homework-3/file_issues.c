#include "file_issues.h"
#include <ctype.h>

/* qsort icin karsilastirma: once sorting_floor, sonra word_id */
static int compare_words(const void *a, const void *b) {
    const WordInfo *wa = (const WordInfo *)a;
    const WordInfo *wb = (const WordInfo *)b;
    if (wa->sorting_floor != wb->sorting_floor)
        return wa->sorting_floor - wb->sorting_floor;
    return wa->word_id - wb->word_id;
}

int read_input_file(const char *path, WordInfo *words, int max_words) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    int count = 0;
    char line[512];
    int line_no = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        int wid, sf;
        char word_buf[MAX_WORD_LEN];
        char *first_space;
        char *second_space;
        char *newline_pos;
        char id_buf[64];
        char sf_buf[64];
        char *endptr;
        size_t len;

        line_no++;
        len = strlen(line);

        if (len == 0 || line[0] == '\n') {
            fprintf(stderr, "Error: Input file contains blank line at line %d\n", line_no);
            fclose(fp);
            return -1;
        }
        if (line[len - 1] != '\n' && !feof(fp)) {
            fprintf(stderr, "Error: Line %d is too long or missing newline\n", line_no);
            fclose(fp);
            return -1;
        }

        if (line[len - 1] != '\n' && feof(fp)) {
            if (len + 1 >= sizeof(line)) {
                fprintf(stderr, "Error: Line %d is too long\n", line_no);
                fclose(fp);
                return -1;
            }
            line[len] = '\n';
            line[len + 1] = '\0';
            len++;
        }

        /* Tam olarak iki adet tek bosluk olmali */
        first_space = strchr(line, ' ');
        second_space = first_space ? strchr(first_space + 1, ' ') : NULL;
        newline_pos = strchr(line, '\n');
        if (!first_space || !second_space) {
            fprintf(stderr, "Error: Invalid spacing at line %d\n", line_no);
            fclose(fp);
            return -1;
        }
        if (first_space[1] == ' ' || second_space[1] == ' ') {
            fprintf(stderr, "Error: Input line %d must use single spaces only\n", line_no);
            fclose(fp);
            return -1;
        }
        if (strchr(second_space + 1, ' ') != NULL) {
            fprintf(stderr, "Error: Input line %d must contain exactly three fields\n", line_no);
            fclose(fp);
            return -1;
        }
        if (line[0] == ' ' || first_space == line || second_space == first_space + 1) {
            fprintf(stderr, "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }
        if (len >= 2 && line[len - 2] == '\r') {
            line[len - 2] = '\n';
            line[len - 1] = '\0';
            len--;
            newline_pos = strchr(line, '\n');
        }

        if (!newline_pos || newline_pos == second_space + 1) {
            fprintf(stderr, "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        size_t id_len = (size_t)(first_space - line);
        size_t word_len = (size_t)(second_space - first_space - 1);
        size_t sf_len = (size_t)(newline_pos - second_space - 1);

        if (id_len == 0 || id_len >= sizeof(id_buf) ||
            word_len == 0 || word_len >= sizeof(word_buf) ||
            sf_len == 0 || sf_len >= sizeof(sf_buf)) {
            fprintf(stderr, "Error: Invalid input format at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        memcpy(id_buf, line, id_len);
        id_buf[id_len] = '\0';
        memcpy(word_buf, first_space + 1, word_len);
        word_buf[word_len] = '\0';
        memcpy(sf_buf, second_space + 1, sf_len);
        sf_buf[sf_len] = '\0';

        errno = 0;
        wid = (int)strtol(id_buf, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            fprintf(stderr, "Error: Invalid word_id at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        errno = 0;
        sf = (int)strtol(sf_buf, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            fprintf(stderr, "Error: Invalid sorting_floor at line %d\n", line_no);
            fclose(fp);
            return -1;
        }

        for (int i = 0; word_buf[i] != '\0'; i++) {
            if (!islower((unsigned char)word_buf[i]) || !isalpha((unsigned char)word_buf[i])) {
                fprintf(stderr, "Error: Word '%s' at line %d must contain lowercase English letters only\n",
                        word_buf, line_no);
                fclose(fp);
                return -1;
            }
        }

        if (count >= max_words) {
            fprintf(stderr, "Error: Too many words in input (max %d)\n", max_words);
            fclose(fp);
            return -1;
        }

        /* WordInfo yapisini doldur */
        WordInfo *w = &words[count];
        memset(w, 0, sizeof(WordInfo));
        w->word_id = wid;
        strncpy(w->word, word_buf, MAX_WORD_LEN - 1);
        w->word[MAX_WORD_LEN - 1] = '\0';
        w->word_len = (int)strlen(w->word);
        w->sorting_floor = sf;
        w->arrival_floor = -1;    /* Henuz bir kata atanmadi */
        w->claimed = 0;
        w->admitted = 0;
        w->completed = 0;

        /* sorting_area, occupied, fixed sifirla */
        memset(w->sorting_area, 0, MAX_WORD_LEN);
        memset(w->occupied, 0, sizeof(w->occupied));
        memset(w->fixed, 0, sizeof(w->fixed));

        /* Her karakter icin CharTask olustur */
        w->num_char_tasks = w->word_len;
        for (int i = 0; i < w->word_len; i++) {
            w->char_tasks[i].word_id = wid;
            w->char_tasks[i].character = w->word[i];
            w->char_tasks[i].original_index = i;
            w->char_tasks[i].src_floor = -1;   /* arrival_floor ataninca set edilecek */
            w->char_tasks[i].dest_floor = sf;
            w->char_tasks[i].claimed = 0;
            w->char_tasks[i].delivered = 0;
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

int write_output_file(const char *path, WordInfo *words, int count) {
    /* Gecici dizi olustur (orijinal diziyi bozmamak icin) */
    WordInfo *sorted = malloc(sizeof(WordInfo) * count);
    if (!sorted) {
        fprintf(stderr, "Error: malloc failed for output sorting\n");
        return -1;
    }
    memcpy(sorted, words, sizeof(WordInfo) * count);

    /* sorting_floor'a gore, sonra word_id'ye gore sirala */
    qsort(sorted, count, sizeof(WordInfo), compare_words);

    FILE *fp = fopen(path, "w");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open output file '%s': %s\n", path, strerror(errno));
        free(sorted);
        return -1;
    }

    /* Her satir: word_id word sorting_floor */
    for (int i = 0; i < count; i++) {
        fprintf(fp, "%d %s %d\n", sorted[i].word_id, sorted[i].word, sorted[i].sorting_floor);
    }

    fclose(fp);
    free(sorted);
    return 0;
}
