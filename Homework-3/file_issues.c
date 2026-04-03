#include "file_issues.h"

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
    int wid, sf;
    char word_buf[MAX_WORD_LEN];

    /* Her satir: word_id word sorting_floor */
    while (fscanf(fp, "%d %s %d", &wid, word_buf, &sf) == 3) {
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