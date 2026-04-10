#include "sorting_process.h"
#include "utils.h"

void sorting_process_run(SharedData *data, int floor_id, int sorter_id) {
    while (data->system_running) {
        int did_work = 0;

        for (int i = 0; i < data->total_words && data->system_running; i++) {
            WordInfo *word = &data->words[i];

            if (word->sorting_floor != floor_id) continue;
            if (!word->admitted || word->completed) continue;

            if (pthread_mutex_trylock(&word->word_mutex) != 0) {
                continue;
            }

            /* BUG FIX: Lock aldiktan sonra completed tekrar kontrol et */
            if (word->completed) {
                pthread_mutex_unlock(&word->word_mutex);
                continue;
            }

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

            if (!has_occupied) {
                pthread_mutex_unlock(&word->word_mutex);
                continue;
            }

            for (int j = 0; j < word->word_len; j++) {
                if (!word->occupied[j]) continue;
                if (word->fixed[j]) continue;

                if (word->sorting_area[j] == word->word[j]) {
                    word->fixed[j] = 1;
                    did_work = 1;
                    log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                            sorter_id, word->sorting_area[j], word->word_id, floor_id);
                    continue;
                }

                char current_char = word->sorting_area[j];
                int target_pos = -1;

                for (int k = 0; k < word->word_len; k++) {
                    if (word->word[k] == current_char && !word->fixed[k]) {
                        target_pos = k;
                        break;
                    }
                }

                if (target_pos == -1) continue;

                if (target_pos == j) {
                    if (word->sorting_area[j] == word->word[j]) {
                        word->fixed[j] = 1;
                        did_work = 1;
                        log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                                sorter_id, word->sorting_area[j], word->word_id, floor_id);
                    }
                    continue;
                }

                if (!word->occupied[target_pos]) {
                    word->sorting_area[target_pos] = current_char;
                    word->occupied[target_pos] = 1;
                    word->sorting_area[j] = '\0';
                    word->occupied[j] = 0;
                    did_work = 1;

                    log_msg("Sorting-process_%d moved char '%c' of word %d to correct index",
                            sorter_id, current_char, word->word_id);

                    if (word->sorting_area[target_pos] == word->word[target_pos]) {
                        word->fixed[target_pos] = 1;
                        log_msg("Sorting-process_%d fixed char '%c' of word %d on floor %d",
                                sorter_id, current_char, word->word_id, floor_id);
                    }
                } else if (!word->fixed[target_pos]) {
                    char temp = word->sorting_area[target_pos];
                    word->sorting_area[target_pos] = current_char;
                    word->sorting_area[j] = temp;
                    did_work = 1;

                    log_msg("Sorting-process_%d swap performed for word %d",
                            sorter_id, word->word_id);

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
            }

            /* Tamamlanma kontrolu */
            all_fixed = 1;
            for (int j = 0; j < word->word_len; j++) {
                if (!word->fixed[j]) {
                    all_fixed = 0;
                    break;
                }
            }

            if (all_fixed) {
                word->completed = 1;
                int saved_arrival = word->arrival_floor;
                int saved_sorting = word->sorting_floor;
                int saved_word_id = word->word_id;

                pthread_mutex_unlock(&word->word_mutex);

                log_msg("Word %d COMPLETED", saved_word_id);

                /* BUG FIX: completed_words sayacini guncelle */
                pthread_mutex_lock(&data->stats_mutex);
                data->completed_words++;
                data->sorting_process_completions[sorter_id]++;
                pthread_mutex_unlock(&data->stats_mutex);

                pthread_mutex_lock(&data->floors[saved_arrival].floor_mutex);
                data->floors[saved_arrival].active_word_count--;
                pthread_cond_broadcast(&data->floors[saved_arrival].floor_cond);
                pthread_mutex_unlock(&data->floors[saved_arrival].floor_mutex);

                if (saved_arrival != saved_sorting) {
                    pthread_mutex_lock(&data->floors[saved_sorting].floor_mutex);
                    data->floors[saved_sorting].active_word_count--;
                    pthread_cond_broadcast(&data->floors[saved_sorting].floor_cond);
                    pthread_mutex_unlock(&data->floors[saved_sorting].floor_mutex);
                }

                pthread_mutex_lock(&data->state_mutex);
                pthread_cond_broadcast(&data->state_cond);
                pthread_mutex_unlock(&data->state_mutex);
                continue;
            }

            pthread_mutex_unlock(&word->word_mutex);
        }

        if (!did_work) {
            pthread_mutex_lock(&data->floors[floor_id].floor_mutex);
            if (data->system_running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->floors[floor_id].floor_cond,
                                       &data->floors[floor_id].floor_mutex, &ts);
            }
            pthread_mutex_unlock(&data->floors[floor_id].floor_mutex);
        }
    }
}