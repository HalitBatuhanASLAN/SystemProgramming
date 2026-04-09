#include "word_carrier_process.h"
#include "utils.h"

void word_carrier_run(SharedData *data, int floor_id, int carrier_id) {
    while (data->system_running) {
        int word_idx = -1;
        int found = 0;

        /* Round-robin ile sahiplenilmemis kelime ara */
        pthread_mutex_lock(&data->round_robin_mutex);

        /* Tum kelimeler admitted mi kontrol et */
        int all_admitted = 1;
        for (int i = 0; i < data->total_words; i++) {
            if (!data->words[i].admitted) {
                all_admitted = 0;
                break;
            }
        }
        if (all_admitted) {
            data->all_words_admitted = 1;
            pthread_mutex_unlock(&data->round_robin_mutex);
            break; /* Bu carrier'in isi bitti */
        }

        /* Round-robin tarama: tum kelimeleri bir tur dola */
        for (int attempt = 0; attempt < data->total_words; attempt++) {
            int idx = (data->round_robin_index + attempt) % data->total_words;
            if (!data->words[idx].claimed && !data->words[idx].admitted) {
                word_idx = idx;
                data->words[idx].claimed = 1;    /* Atomik claim */
                data->round_robin_index = (idx + 1) % data->total_words;
                found = 1;
                break;
            }
        }

        pthread_mutex_unlock(&data->round_robin_mutex);

        if (!found) {
            /* Uygun kelime yoksa kapasite veya tamamlanma degisimi bekle */
            pthread_mutex_lock(&data->state_mutex);
            if (data->system_running && !data->all_words_admitted) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->state_cond, &data->state_mutex, &ts);
            }
            pthread_mutex_unlock(&data->state_mutex);
            continue;
        }

        /* Kelime bulundu, simdi kapasite kontrolu yap (all-or-nothing) */
        WordInfo *word = &data->words[word_idx];
        int arrival_floor = floor_id;
        int sorting_floor = word->sorting_floor;

        /*
         * LOCK ORDERING: deadlock onlemek icin her zaman kucuk floor ID'yi once kilitle.
         * Ayni kat ise tek kilit yeterli.
         */
        int lock_first = (arrival_floor <= sorting_floor) ? arrival_floor : sorting_floor;
        int lock_second = (arrival_floor <= sorting_floor) ? sorting_floor : arrival_floor;

        pthread_mutex_lock(&data->floors[lock_first].floor_mutex);
        if (lock_first != lock_second) {
            pthread_mutex_lock(&data->floors[lock_second].floor_mutex);
        }

        /* Her iki katin kapasitesi uygun mu? */
        int arrival_ok = (data->floors[arrival_floor].active_word_count
                          < data->config.max_words_per_floor);
        int sorting_ok = (data->floors[sorting_floor].active_word_count
                          < data->config.max_words_per_floor);

        if (arrival_ok && sorting_ok) {
            /* Kabul: her iki katin da sayacini artir */
            data->floors[arrival_floor].active_word_count++;
            if (arrival_floor != sorting_floor) {
                data->floors[sorting_floor].active_word_count++;
            }

            /* Kilitleri birak */
            if (lock_first != lock_second) {
                pthread_mutex_unlock(&data->floors[lock_second].floor_mutex);
            }
            pthread_mutex_unlock(&data->floors[lock_first].floor_mutex);

            /* Kelimeyi sisteme al */
            pthread_mutex_lock(&word->word_mutex);
            word->admitted = 1;
            word->arrival_floor = arrival_floor;

            /* Char task'lerin src_floor'unu set et */
            for (int i = 0; i < word->num_char_tasks; i++) {
                word->char_tasks[i].src_floor = arrival_floor;
            }
            pthread_mutex_unlock(&word->word_mutex);

            pthread_mutex_lock(&data->stats_mutex);
            data->word_carrier_admissions[carrier_id]++;
            pthread_mutex_unlock(&data->stats_mutex);

            pthread_mutex_lock(&data->floors[arrival_floor].floor_mutex);
            pthread_cond_broadcast(&data->floors[arrival_floor].floor_cond);
            pthread_mutex_unlock(&data->floors[arrival_floor].floor_mutex);

            pthread_mutex_lock(&data->state_mutex);
            pthread_cond_broadcast(&data->state_cond);
            pthread_mutex_unlock(&data->state_mutex);

            log_msg("Word-carrier-process_%d claimed word %d", carrier_id, word->word_id);
            log_msg("Word %d admitted to floor %d (sorting floor: %d)",
                    word->word_id, arrival_floor, sorting_floor);
        } else {
            /* Red: kelimeyi birak, ileride tekrar denenebilir */
            if (lock_first != lock_second) {
                pthread_mutex_unlock(&data->floors[lock_second].floor_mutex);
            }
            pthread_mutex_unlock(&data->floors[lock_first].floor_mutex);

            /* Claimed flag'i geri al */
            pthread_mutex_lock(&data->round_robin_mutex);
            word->claimed = 0;
            pthread_mutex_unlock(&data->round_robin_mutex);

            /* Istatistik: retry sayisini artir */
            pthread_mutex_lock(&data->stats_mutex);
            data->total_retries++;
            pthread_mutex_unlock(&data->stats_mutex);

            pthread_mutex_lock(&data->state_mutex);
            if (data->system_running) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 100000000; /* 100ms */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&data->state_cond, &data->state_mutex, &ts);
            }
            pthread_mutex_unlock(&data->state_mutex);
        }
    }
}
