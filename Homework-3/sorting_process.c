#include "sorting_process.h"
#include "utils.h"

void sorting_process_run(SharedData *data, int floor_id, int sorter_id) {
    while (data->system_running) {
        int did_work = 0;

        /* Bu kattaki tum kelimeleri tara */
        for (int i = 0; i < data->total_words && data->system_running; i++) {
            WordInfo *word = &data->words[i];

            /* Bu kelime bu katta siralanacak mi? */
            if (word->sorting_floor != floor_id) continue;
            if (!word->admitted || word->completed) continue;

            /* Per-word lock al (trylock: basarisizsa bir sonraki kelimeye gec) */
            if (pthread_mutex_trylock(&word->word_mutex) != 0) {
                continue; /* Baska bir sorter calisiyor */
            }

            /* Sorting area'da islem yap */
            int all_fixed = 1;
            int has_occupied = 0;

            for (int j = 0; j < word->word_len; j++) {
                if (!word->occupied[j] || !word->fixed[j]) {
                    all_fixed = 0;
                }
                if (word->occupied[j]) {
                    has_occupied = 1;
                }
            }

            /* Hic karakter gelmemisse devam et */
            if (!has_occupied) {
                pthread_mutex_unlock(&word->word_mutex);
                continue;
            }

            /* Her slot icin siralama kurallari uygula */
            for (int j = 0; j < word->word_len; j++) {
                /* Case 1: Bos -> hicbir sey yapma */
                if (!word->occupied[j]) continue;

                /* Case 2: Zaten fixed -> hicbir sey yapma */
                if (word->fixed[j]) continue;

                /* Case 3: Dogru pozisyonda mi? */
                if (word->sorting_area[j] == word->word[j]) {
                    word->fixed[j] = 1;
                    did_work = 1;
                    log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                            sorter_id, word->sorting_area[j], word->word_id, floor_id);
                    continue;
                }

                /* Case 4: Yanlis pozisyonda -> dogru hedefi bul */
                char current_char = word->sorting_area[j];
                int target_pos = -1;

                /* Bu karakterin olmasi gereken pozisyonu bul */
                for (int k = 0; k < word->word_len; k++) {
                    if (word->word[k] == current_char && !word->fixed[k]) {
                        target_pos = k;
                        break;
                    }
                }

                if (target_pos == -1 || target_pos == j) continue;

                if (!word->occupied[target_pos]) {
                    /* Hedef bos -> tasi */
                    word->sorting_area[target_pos] = current_char;
                    word->occupied[target_pos] = 1;
                    word->sorting_area[j] = '\0';
                    word->occupied[j] = 0;
                    did_work = 1;

                    log_msg("Sorting-process_%d moved char '%c' of word %d to correct index",
                            sorter_id, current_char, word->word_id);

                    /* Tasindiktan sonra dogru pozisyona geldi mi? */
                    if (word->sorting_area[target_pos] == word->word[target_pos]) {
                        word->fixed[target_pos] = 1;
                        log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                                sorter_id, current_char, word->word_id, floor_id);
                    }
                } else if (!word->fixed[target_pos]) {
                    /* Hedef dolu ve fixed degil -> swap */
                    char temp = word->sorting_area[target_pos];
                    word->sorting_area[target_pos] = current_char;
                    word->sorting_area[j] = temp;
                    did_work = 1;

                    log_msg("Sorting-process_%d swap performed for word %d",
                            sorter_id, word->word_id);

                    /* Swap sonrasi kontrol */
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
                /* Hedef fixed ise -> hicbir sey yapma */
            }

            /* Tamamlanma kontrolu: tum pozisyonlar fixed mi? */
            all_fixed = 1;
            for (int j = 0; j < word->word_len; j++) {
                if (!word->fixed[j]) {
                    all_fixed = 0;
                    break;
                }
            }

            if (all_fixed) {
                word->completed = 1;
                log_msg("Word %d COMPLETED", word->word_id);

                /* Kat kapasitelerini serbest birak */
                pthread_mutex_lock(&data->floors[word->arrival_floor].floor_mutex);
                data->floors[word->arrival_floor].active_word_count--;
                pthread_mutex_unlock(&data->floors[word->arrival_floor].floor_mutex);

                if (word->arrival_floor != word->sorting_floor) {
                    pthread_mutex_lock(&data->floors[word->sorting_floor].floor_mutex);
                    data->floors[word->sorting_floor].active_word_count--;
                    pthread_mutex_unlock(&data->floors[word->sorting_floor].floor_mutex);
                }
            }

            pthread_mutex_unlock(&word->word_mutex);
        }

        /* Is yapilmadiysa kisa bekle (busy-waiting onleme) */
        if (!did_work) {
            usleep(10000); /* 10ms */
        }
    }
}