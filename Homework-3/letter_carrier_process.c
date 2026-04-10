#include "letter_carrier_process.h"
#include "utils.h"

/* Delivery elevator'a istek gonder */
static void request_delivery_elevator(SharedData *data, int from_floor, int to_floor,
                                       int carrier_id, int word_id, char character,
                                       int original_index) {
    ElevatorState *elev = &data->delivery_elevator;

    pthread_mutex_lock(&elev->elev_mutex);
    if (elev->queue_size < MAX_QUEUE_SIZE) {
        ElevatorRequest *req = &elev->queue[elev->queue_size];
        req->requester_type  = 0;
        req->from_floor      = from_floor;
        req->to_floor        = to_floor;
        req->carrier_id      = carrier_id;
        req->word_id         = word_id;
        req->character       = character;
        req->original_index  = original_index;
        req->served          = 0;
        elev->queue_size++;
    }
    pthread_mutex_unlock(&elev->elev_mutex);

    sem_post(&elev->request_sem);
    pthread_mutex_lock(&elev->elev_mutex);
    pthread_cond_broadcast(&elev->elev_cond);
    pthread_mutex_unlock(&elev->elev_mutex);
}

/* Reposition elevator'a istek gonder */
static void request_reposition_elevator(SharedData *data, int from_floor, int carrier_id,
                                         int *out_to_floor) {
    ElevatorState *elev = &data->reposition_elevator;
    int to_floor;

    do {
        to_floor = rand_range(data->config.num_floors);
    } while (to_floor == from_floor && data->config.num_floors > 1);

    *out_to_floor = to_floor;

    pthread_mutex_lock(&elev->elev_mutex);
    if (elev->queue_size < MAX_QUEUE_SIZE) {
        ElevatorRequest *req = &elev->queue[elev->queue_size];
        req->requester_type  = 1;
        req->from_floor      = from_floor;
        req->to_floor        = to_floor;
        req->carrier_id      = carrier_id;
        req->word_id         = -1;
        req->character       = '\0';
        req->original_index  = -1;
        req->served          = 0;
        elev->queue_size++;
    }
    pthread_mutex_unlock(&elev->elev_mutex);

    sem_post(&elev->request_sem);
    pthread_mutex_lock(&elev->elev_mutex);
    pthread_cond_broadcast(&elev->elev_cond);
    pthread_mutex_unlock(&elev->elev_mutex);
}

/* Hedef kata karakteri yerlestir */
static void deliver_char_to_sorting_area(SharedData *data, int word_idx, char character,
                                          int original_index, int carrier_id) {
    (void)original_index;
    WordInfo *word = &data->words[word_idx];

    pthread_mutex_lock(&word->word_mutex);

    for (int i = 0; i < word->word_len; i++) {
        if (!word->occupied[i] && !word->fixed[i]) {
            word->sorting_area[i] = character;
            word->occupied[i] = 1;
            pthread_mutex_unlock(&word->word_mutex);

            log_msg("Letter-carrier-process_%d brought char '%c' of word %d to floor %d",
                    carrier_id, character, word->word_id, word->sorting_floor);

            pthread_mutex_lock(&data->stats_mutex);
            data->total_chars_transported++;
            data->letter_carrier_transports[carrier_id]++;
            pthread_mutex_unlock(&data->stats_mutex);

            pthread_mutex_lock(&data->floors[word->sorting_floor].floor_mutex);
            pthread_cond_broadcast(&data->floors[word->sorting_floor].floor_cond);
            pthread_mutex_unlock(&data->floors[word->sorting_floor].floor_mutex);

            pthread_mutex_lock(&data->state_mutex);
            pthread_cond_broadcast(&data->state_cond);
            pthread_mutex_unlock(&data->state_mutex);
            return;
        }
    }

    pthread_mutex_unlock(&word->word_mutex);
}

/* Bulundugu katta is ara */
static int find_task_on_floor(SharedData *data, int current_floor,
                               int *out_word_idx, int *out_char_idx) {
    int start_word = rand_range(data->total_words);
    for (int attempt = 0; attempt < data->total_words; attempt++) {
        int i = (start_word + attempt) % data->total_words;
        WordInfo *w = &data->words[i];

        if (!w->admitted || w->completed) continue;
        if (w->arrival_floor != current_floor) continue;

        pthread_mutex_lock(&w->word_mutex);

        int start_char = rand_range(w->num_char_tasks);
        for (int jj = 0; jj < w->num_char_tasks; jj++) {
            int j = (start_char + jj) % w->num_char_tasks;
            if (!w->char_tasks[j].claimed && !w->char_tasks[j].delivered) {
                w->char_tasks[j].claimed = 1;
                *out_word_idx = i;
                *out_char_idx = j;
                pthread_mutex_unlock(&w->word_mutex);
                return 1;
            }
        }

        pthread_mutex_unlock(&w->word_mutex);
    }

    return 0;
}

void letter_carrier_run(SharedData *data, int initial_floor, int carrier_id) {
    int current_floor = initial_floor;

    while (data->system_running) {
        int word_idx, char_idx;

        if (find_task_on_floor(data, current_floor, &word_idx, &char_idx)) {
            WordInfo *word  = &data->words[word_idx];
            CharTask *task  = &word->char_tasks[char_idx];

            log_msg("Letter-carrier-process_%d selected char '%c' of word %d from floor %d",
                    carrier_id, task->character, word->word_id, current_floor);

            if (task->dest_floor == current_floor) {
                log_msg("Destination is same floor -> direct placement");

                deliver_char_to_sorting_area(data, word_idx, task->character,
                                              task->original_index, carrier_id);

                pthread_mutex_lock(&word->word_mutex);
                task->delivered = 1;
                pthread_mutex_unlock(&word->word_mutex);
            } else {
                /* Farkli kat: delivery elevator kullan */
                log_msg("Letter-carrier-process_%d requested delivery elevator from floor %d to floor %d",
                        carrier_id, current_floor, task->dest_floor);

                int dest = task->dest_floor;
                int widx = word->word_id;
                int oidx = task->original_index;
                char ch  = task->character;

                request_delivery_elevator(data, current_floor, dest,
                                           carrier_id, widx, ch, oidx);

                /*
                 * BUG FIX: Asansor bekleme - served=1 aninda to_floor'u kaydet,
                 * clean_served_requests() silmeden once degeri okuyoruz.
                 * Yontem: istek kuyruktan silinmeden once to_floor'u biliyor olacagiz
                 * cunku to_floor zaten bizim gonderdiklerimizle ayni (dest).
                 * Sadece served bayragi 1 veya 2 olmasini bekliyoruz.
                 */
                ElevatorState *elev = &data->delivery_elevator;
                int arrived = 0;
                while (!arrived && data->system_running) {
                    pthread_mutex_lock(&elev->elev_mutex);
                    for (int k = 0; k < elev->queue_size; k++) {
                        if (elev->queue[k].carrier_id    == carrier_id &&
                            elev->queue[k].word_id       == widx       &&
                            elev->queue[k].original_index == oidx      &&
                            elev->queue[k].served == 1) {
                            arrived = 1;
                            break;
                        }
                    }
                    /* served=1 olan istek temizlenmis olabilir - o zaman da varmisizdir */
                    if (!arrived && data->system_running) {
                        /*
                         * Eger istek kuyrukta hic yoksa (temizlendi) ve
                         * served=2 de yoksa: teslim edildi demektir.
                         */
                        int still_in_queue = 0;
                        for (int k = 0; k < elev->queue_size; k++) {
                            if (elev->queue[k].carrier_id    == carrier_id &&
                                elev->queue[k].word_id       == widx       &&
                                elev->queue[k].original_index == oidx) {
                                still_in_queue = 1;
                                break;
                            }
                        }
                        if (!still_in_queue) {
                            /* Istek kuyruktan kaldirildi = teslim edildi */
                            arrived = 1;
                        }
                    }
                    if (!arrived && data->system_running) {
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_nsec += 50000000; /* 50ms */
                        if (ts.tv_nsec >= 1000000000) {
                            ts.tv_sec++;
                            ts.tv_nsec -= 1000000000;
                        }
                        pthread_cond_timedwait(&elev->elev_cond, &elev->elev_mutex, &ts);
                    }
                    pthread_mutex_unlock(&elev->elev_mutex);
                }

                if (arrived) {
                    /* BUG FIX: letter_carrier_count guncelle */
                    if (current_floor != dest) {
                        pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                        data->floors[current_floor].letter_carrier_count--;
                        pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);

                        pthread_mutex_lock(&data->floors[dest].floor_mutex);
                        data->floors[dest].letter_carrier_count++;
                        pthread_mutex_unlock(&data->floors[dest].floor_mutex);
                    }
                    current_floor = dest;

                    deliver_char_to_sorting_area(data, word_idx, ch, oidx, carrier_id);

                    pthread_mutex_lock(&word->word_mutex);
                    task->delivered = 1;
                    pthread_mutex_unlock(&word->word_mutex);
                }
            }
        } else {
            /* Is bulunamadi: reposition elevator */
            log_msg("Letter-carrier-process_%d found no available task on floor %d",
                    carrier_id, current_floor);

            if (data->config.num_floors > 1) {
                log_msg("Letter-carrier-process_%d requested reposition elevator from floor %d",
                        carrier_id, current_floor);

                int to_floor = current_floor;
                request_reposition_elevator(data, current_floor, carrier_id, &to_floor);

                ElevatorState *elev = &data->reposition_elevator;
                int arrived = 0;
                int new_floor = to_floor; /* Zaten biliyoruz hangi kata gidecegini */
                while (!arrived && data->system_running) {
                    pthread_mutex_lock(&elev->elev_mutex);

                    for (int k = 0; k < elev->queue_size; k++) {
                        if (elev->queue[k].carrier_id == carrier_id &&
                            elev->queue[k].to_floor   == to_floor   &&
                            elev->queue[k].served == 1) {
                            arrived = 1;
                            break;
                        }
                    }
                    /* Temizlenmis olabilir */
                    if (!arrived && data->system_running) {
                        int still_in_queue = 0;
                        for (int k = 0; k < elev->queue_size; k++) {
                            if (elev->queue[k].carrier_id == carrier_id &&
                                elev->queue[k].to_floor   == to_floor) {
                                still_in_queue = 1;
                                break;
                            }
                        }
                        if (!still_in_queue) {
                            arrived = 1;
                        }
                    }
                    if (!arrived && data->system_running) {
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_nsec += 50000000; /* 50ms */
                        if (ts.tv_nsec >= 1000000000) {
                            ts.tv_sec++;
                            ts.tv_nsec -= 1000000000;
                        }
                        pthread_cond_timedwait(&elev->elev_cond, &elev->elev_mutex, &ts);
                    }
                    pthread_mutex_unlock(&elev->elev_mutex);
                }

                if (arrived) {
                    int old_floor = current_floor;
                    current_floor = new_floor;

                    if (old_floor != current_floor) {
                        pthread_mutex_lock(&data->floors[old_floor].floor_mutex);
                        data->floors[old_floor].letter_carrier_count--;
                        pthread_cond_broadcast(&data->floors[old_floor].floor_cond);
                        pthread_mutex_unlock(&data->floors[old_floor].floor_mutex);

                        pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                        data->floors[current_floor].letter_carrier_count++;
                        pthread_cond_broadcast(&data->floors[current_floor].floor_cond);
                        pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);
                    }

                    pthread_mutex_lock(&data->state_mutex);
                    pthread_cond_broadcast(&data->state_cond);
                    pthread_mutex_unlock(&data->state_mutex);

                    log_msg("Letter-carrier-process_%d resumed work on floor %d",
                            carrier_id, current_floor);
                }
            } else {
                /* Tek kat: bekle */
                pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                if (data->system_running) {
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_nsec += 100000000; /* 100ms */
                    if (ts.tv_nsec >= 1000000000) {
                        ts.tv_sec++;
                        ts.tv_nsec -= 1000000000;
                    }
                    pthread_cond_timedwait(&data->floors[current_floor].floor_cond,
                                           &data->floors[current_floor].floor_mutex, &ts);
                }
                pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);
            }
        }
    }
}