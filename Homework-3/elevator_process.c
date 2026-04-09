#include "elevator_process.h"
#include "utils.h"

/*
 * Asansor calisma politikasi (her iki asansor icin ayni):
 * 1. Yon bazli hareket (UP/DOWN)
 * 2. Mevcut yondeki isteklere once hizmet ver
 * 3. En ust/alt katta yon degistirmek ZORUNLU
 * 4. Su iki kosul saglanana kadar mevcut yonde devam et:
 *    a) Mevcut yonde (yukari/asagi) baska istek yok
 *    b) Asansor bos (idle)
 * 5. Tamamen bosta ise ilk gelen istegin yonunu al
 */

/* Kuyruktan tamamlanmis (served=1) istekleri temizle */
static void clean_served_requests(ElevatorState *elev) {
    int write_idx = 0;
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served != 1) {
            if (write_idx != i) {
                elev->queue[write_idx] = elev->queue[i];
            }
            write_idx++;
        }
    }
    elev->queue_size = write_idx;
}

/* Herhangi bir yonde bekleyen istek var mi? */
static int has_any_pending(ElevatorState *elev) {
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served == 0 || elev->queue[i].served == 2)
            return 1;
    }
    return 0;
}

/* Ilk bekleyen istegin yonunu belirle */
static int determine_direction(ElevatorState *elev) {
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served == 0) {
            if (elev->queue[i].from_floor > elev->current_floor) return DIR_UP;
            if (elev->queue[i].from_floor < elev->current_floor) return DIR_DOWN;
            /* Ayni katta: hedef kata gore yon belirle */
            if (elev->queue[i].to_floor > elev->current_floor) return DIR_UP;
            if (elev->queue[i].to_floor < elev->current_floor) return DIR_DOWN;
        }
    }
    return DIR_IDLE;
}

/* ========== DELIVERY ELEVATOR ========== */

void delivery_elevator_run(SharedData *data) {
    ElevatorState *elev = &data->delivery_elevator;
    int num_floors = data->config.num_floors;

    while (data->system_running) {
        /* Istek bekle (busy-waiting yerine semaphore) */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000; /* 50ms timeout */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        sem_timedwait(&elev->request_sem, &ts);

        if (!data->system_running) break;

        pthread_mutex_lock(&elev->elev_mutex);

        /* Tamamlanmis istekleri temizle */
        clean_served_requests(elev);

        /* Bekleyen istek yoksa devam et */
        if (!has_any_pending(elev)) {
            elev->direction = DIR_IDLE;
            pthread_mutex_unlock(&elev->elev_mutex);
            continue;
        }

        /* Yon belirle (idle ise ilk istegin yonune gore) */
        if (elev->direction == DIR_IDLE) {
            elev->direction = determine_direction(elev);
            if (elev->direction == DIR_IDLE) {
                pthread_mutex_unlock(&elev->elev_mutex);
                continue;
            }
        }

        /* --- Mevcut katta islem yap --- */

        /* 1. Indirilecek yolculari indir */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 2 &&
                elev->queue[i].to_floor == elev->current_floor) {

                elev->queue[i].served = 1; /* Teslim edildi */
                elev->current_load--;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Delivery elevator drop off at floor %d (currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d carrying char '%c' of word %d",
                        elev->current_floor, elev->current_load,
                        elev->queue[i].carrier_id, elev->queue[i].character,
                        elev->queue[i].word_id);

                pthread_mutex_lock(&data->stats_mutex);
                data->delivery_elevator_ops++;
                pthread_mutex_unlock(&data->stats_mutex);

                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* 2. Alinacak yolculari al (kapasite dahilinde) */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 0 &&
                elev->queue[i].from_floor == elev->current_floor &&
                elev->current_load < elev->capacity) {

                elev->queue[i].served = 2; /* Asansore alindi */
                elev->current_load++;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Delivery elevator pick up (currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d carrying char '%c' of word %d",
                        elev->current_load,
                        elev->queue[i].carrier_id, elev->queue[i].character,
                        elev->queue[i].word_id);
                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* --- Sonraki kat hesapla --- */

        /* Mevcut yonde devam etmeli mi? */
        int should_continue = 0;

        /* Mevcut yonde alinacak veya indirilecek istek var mi? */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 1) continue; /* Tamamlanmis */

            int relevant_floor = -1;
            if (elev->queue[i].served == 0)
                relevant_floor = elev->queue[i].from_floor;
            else if (elev->queue[i].served == 2)
                relevant_floor = elev->queue[i].to_floor;

            if (relevant_floor < 0) continue;

            if (elev->direction == DIR_UP && relevant_floor > elev->current_floor) {
                should_continue = 1;
                break;
            }
            if (elev->direction == DIR_DOWN && relevant_floor < elev->current_floor) {
                should_continue = 1;
                break;
            }
        }

        if (!should_continue && elev->current_load == 0) {
            /* Mevcut yonde is kalmadi ve asansor bos -> yon degistir veya idle */
            if (has_any_pending(elev)) {
                elev->direction = determine_direction(elev);
            } else {
                elev->direction = DIR_IDLE;
            }
        }

        /* En ust veya alt katta ZORUNLU yon degistir */
        if (elev->current_floor >= num_floors - 1 && elev->direction == DIR_UP) {
            elev->direction = DIR_DOWN;
        } else if (elev->current_floor <= 0 && elev->direction == DIR_DOWN) {
            elev->direction = DIR_UP;
        }

        /* Bir kat hareket et */
        if (elev->direction == DIR_UP && elev->current_floor < num_floors - 1) {
            elev->current_floor++;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Delivery elevator moving UP");
            log_msg("Delivery elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        } else if (elev->direction == DIR_DOWN && elev->current_floor > 0) {
            elev->current_floor--;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Delivery elevator moving DOWN");
            log_msg("Delivery elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        }

        pthread_mutex_unlock(&elev->elev_mutex);
    }
}

/* ========== REPOSITION ELEVATOR ========== */

void reposition_elevator_run(SharedData *data) {
    ElevatorState *elev = &data->reposition_elevator;
    int num_floors = data->config.num_floors;

    while (data->system_running) {
        /* Istek bekle */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000; /* 50ms timeout */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        sem_timedwait(&elev->request_sem, &ts);

        if (!data->system_running) break;

        pthread_mutex_lock(&elev->elev_mutex);

        clean_served_requests(elev);

        if (!has_any_pending(elev)) {
            elev->direction = DIR_IDLE;
            pthread_mutex_unlock(&elev->elev_mutex);
            continue;
        }

        if (elev->direction == DIR_IDLE) {
            elev->direction = determine_direction(elev);
            if (elev->direction == DIR_IDLE) {
                pthread_mutex_unlock(&elev->elev_mutex);
                continue;
            }
        }

        /* Mevcut katta indir */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 2 &&
                elev->queue[i].to_floor == elev->current_floor) {

                elev->queue[i].served = 1;
                elev->current_load--;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Reposition elevator drop off at floor %d (currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d",
                        elev->current_floor, elev->current_load,
                        elev->queue[i].carrier_id);

                pthread_mutex_lock(&data->stats_mutex);
                data->reposition_elevator_ops++;
                pthread_mutex_unlock(&data->stats_mutex);

                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* Mevcut katta al */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 0 &&
                elev->queue[i].from_floor == elev->current_floor &&
                elev->current_load < elev->capacity) {

                elev->queue[i].served = 2;
                elev->current_load++;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Reposition elevator pick up (currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d",
                        elev->current_load,
                        elev->queue[i].carrier_id);
                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* Yon kontrolu */
        int should_continue = 0;
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 1) continue;

            int relevant_floor = -1;
            if (elev->queue[i].served == 0)
                relevant_floor = elev->queue[i].from_floor;
            else if (elev->queue[i].served == 2)
                relevant_floor = elev->queue[i].to_floor;

            if (relevant_floor < 0) continue;

            if (elev->direction == DIR_UP && relevant_floor > elev->current_floor) {
                should_continue = 1; break;
            }
            if (elev->direction == DIR_DOWN && relevant_floor < elev->current_floor) {
                should_continue = 1; break;
            }
        }

        if (!should_continue && elev->current_load == 0) {
            if (has_any_pending(elev))
                elev->direction = determine_direction(elev);
            else
                elev->direction = DIR_IDLE;
        }

        /* Zorunlu yon degisimi (ust/alt sinir) */
        if (elev->current_floor >= num_floors - 1 && elev->direction == DIR_UP)
            elev->direction = DIR_DOWN;
        else if (elev->current_floor <= 0 && elev->direction == DIR_DOWN)
            elev->direction = DIR_UP;

        /* Hareket */
        if (elev->direction == DIR_UP && elev->current_floor < num_floors - 1) {
            elev->current_floor++;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Reposition elevator moving UP");
            log_msg("Reposition elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        } else if (elev->direction == DIR_DOWN && elev->current_floor > 0) {
            elev->current_floor--;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Reposition elevator moving DOWN");
            log_msg("Reposition elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        }

        pthread_mutex_unlock(&elev->elev_mutex);
    }
}
