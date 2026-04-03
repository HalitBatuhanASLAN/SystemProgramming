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
        req->requester_type = 0; /* delivery */
        req->from_floor = from_floor;
        req->to_floor = to_floor;
        req->carrier_id = carrier_id;
        req->word_id = word_id;
        req->character = character;
        req->original_index = original_index;
        req->served = 0;
        elev->queue_size++;
    }
    pthread_mutex_unlock(&elev->elev_mutex);

    /* Asansore yeni istek geldigini bildir */
    sem_post(&elev->request_sem);
}

/* Reposition elevator'a istek gonder */
static void request_reposition_elevator(SharedData *data, int from_floor, int carrier_id) {
    ElevatorState *elev = &data->reposition_elevator;
    int to_floor;

    /* Rastgele farkli bir kat sec */
    do {
        to_floor = rand_range(data->config.num_floors);
    } while (to_floor == from_floor && data->config.num_floors > 1);

    pthread_mutex_lock(&elev->elev_mutex);
    if (elev->queue_size < MAX_QUEUE_SIZE) {
        ElevatorRequest *req = &elev->queue[elev->queue_size];
        req->requester_type = 1; /* reposition */
        req->from_floor = from_floor;
        req->to_floor = to_floor;
        req->carrier_id = carrier_id;
        req->word_id = -1;
        req->character = '\0';
        req->original_index = -1;
        req->served = 0;
        elev->queue_size++;
    }
    pthread_mutex_unlock(&elev->elev_mutex);

    sem_post(&elev->request_sem);
}

/* Hedef kata karakteri yerlestir (sorting area'ya) */
static void deliver_char_to_sorting_area(SharedData *data, int word_idx, char character,
                                          int original_index, int carrier_id) {
    (void)original_index; /* Ileride kullanilabilir */
    WordInfo *word = &data->words[word_idx];

    pthread_mutex_lock(&word->word_mutex);

    /* Ilk bos, fixed olmayan slota yerlestir */
    for (int i = 0; i < word->word_len; i++) {
        if (!word->occupied[i] && !word->fixed[i]) {
            word->sorting_area[i] = character;
            word->occupied[i] = 1;
            pthread_mutex_unlock(&word->word_mutex);

            log_msg("Letter-carrier-process_%d brought char '%c' of word %d to floor %d",
                    carrier_id, character, word->word_id, word->sorting_floor);

            /* Istatistik guncelle */
            pthread_mutex_lock(&data->stats_mutex);
            data->total_chars_transported++;
            pthread_mutex_unlock(&data->stats_mutex);
            return;
        }
    }

    pthread_mutex_unlock(&word->word_mutex);
}

/* Bulundugu katta is ara */
static int find_task_on_floor(SharedData *data, int current_floor,
                               int *out_word_idx, int *out_char_idx) {
    /* Kattaki admitted kelimeleri tara */
    for (int i = 0; i < data->total_words; i++) {
        WordInfo *w = &data->words[i];

        /* Bu kelime bu katta mi ve admitted mi? */
        if (!w->admitted || w->completed) continue;
        if (w->arrival_floor != current_floor) continue;

        pthread_mutex_lock(&w->word_mutex);

        /* Sahiplenilmemis, teslim edilmemis harf ara */
        for (int j = 0; j < w->num_char_tasks; j++) {
            if (!w->char_tasks[j].claimed && !w->char_tasks[j].delivered) {
                w->char_tasks[j].claimed = 1; /* Atomik claim */
                *out_word_idx = i;
                *out_char_idx = j;
                pthread_mutex_unlock(&w->word_mutex);
                return 1; /* Gorev bulundu */
            }
        }

        pthread_mutex_unlock(&w->word_mutex);
    }

    return 0; /* Gorev bulunamadi */
}

void letter_carrier_run(SharedData *data, int initial_floor, int carrier_id) {
    int current_floor = initial_floor;

    while (data->system_running) {
        int word_idx, char_idx;

        /* Bulundugu katta is ara */
        if (find_task_on_floor(data, current_floor, &word_idx, &char_idx)) {
            WordInfo *word = &data->words[word_idx];
            CharTask *task = &word->char_tasks[char_idx];

            log_msg("Letter-carrier-process_%d selected char '%c' of word %d from floor %d",
                    carrier_id, task->character, word->word_id, current_floor);

            if (task->dest_floor == current_floor) {
                /* Ayni kat: direkt yerlestir */
                log_msg("Destination is same floor -> direct placement");

                deliver_char_to_sorting_area(data, word_idx, task->character,
                                              task->original_index, carrier_id);

                /* Gorevi tamamla */
                pthread_mutex_lock(&word->word_mutex);
                task->delivered = 1;
                pthread_mutex_unlock(&word->word_mutex);
            } else {
                /* Farkli kat: delivery elevator kullan */
                log_msg("Letter-carrier-process_%d requested delivery elevator from floor %d to floor %d",
                        carrier_id, current_floor, task->dest_floor);

                request_delivery_elevator(data, current_floor, task->dest_floor,
                                           carrier_id, word->word_id,
                                           task->character, task->original_index);

                /* Elevator'un teslimi tamamlamasini bekle */
                /* Basit yaklasim: served flag'i kontrol et */
                ElevatorState *elev = &data->delivery_elevator;
                int served = 0;
                while (!served && data->system_running) {
                    pthread_mutex_lock(&elev->elev_mutex);
                    /* Bizim istegimizi bul */
                    for (int i = 0; i < elev->queue_size; i++) {
                        if (elev->queue[i].carrier_id == carrier_id &&
                            elev->queue[i].word_id == word->word_id &&
                            elev->queue[i].original_index == task->original_index &&
                            elev->queue[i].served) {
                            served = 1;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&elev->elev_mutex);

                    if (!served) usleep(5000); /* 5ms bekle */
                }

                if (served) {
                    /* Hedef kata ulasildi */
                    current_floor = task->dest_floor;

                    deliver_char_to_sorting_area(data, word_idx, task->character,
                                                  task->original_index, carrier_id);

                    pthread_mutex_lock(&word->word_mutex);
                    task->delivered = 1;
                    pthread_mutex_unlock(&word->word_mutex);
                }
            }
        } else {
            /* Is bulunamadi: reposition elevator ile baska kata git */
            log_msg("Letter-carrier-process_%d found no available task on floor %d",
                    carrier_id, current_floor);

            if (data->config.num_floors > 1) {
                log_msg("Letter-carrier-process_%d requested reposition elevator from floor %d",
                        carrier_id, current_floor);

                /* Kattan ayriliyor: letter_carrier_count azalt */
                pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                data->floors[current_floor].letter_carrier_count--;
                pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);

                request_reposition_elevator(data, current_floor, carrier_id);

                /* Reposition tamamlanmasini bekle */
                ElevatorState *elev = &data->reposition_elevator;
                int served = 0;
                int new_floor = current_floor;
                while (!served && data->system_running) {
                    pthread_mutex_lock(&elev->elev_mutex);
                    for (int i = 0; i < elev->queue_size; i++) {
                        if (elev->queue[i].carrier_id == carrier_id &&
                            elev->queue[i].served) {
                            served = 1;
                            new_floor = elev->queue[i].to_floor;
                            break;
                        }
                    }
                    pthread_mutex_unlock(&elev->elev_mutex);

                    if (!served) usleep(5000);
                }

                if (served) {
                    current_floor = new_floor;

                    /* Yeni kata varis: letter_carrier_count artir */
                    pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                    data->floors[current_floor].letter_carrier_count++;
                    pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);

                    log_msg("Letter-carrier-process_%d resumed work on floor %d",
                            carrier_id, current_floor);
                }
            } else {
                /* Tek kat var, bekle */
                usleep(10000);
            }
        }
    }
}