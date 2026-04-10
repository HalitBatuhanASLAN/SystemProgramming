#include "mutex_semaphore.h"

/* Process'ler arasi kullanilabilir mutex olusturur */
static int init_shared_mutex(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) return -1;
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_mutexattr_destroy(&attr);
        return -1;
    }
    int ret = pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    return (ret == 0) ? 0 : -1;
}

/* Process'ler arasi kullanilabilir condition variable olusturur */
static int init_shared_cond(pthread_cond_t *cond) {
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) != 0) return -1;
    if (pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        pthread_condattr_destroy(&attr);
        return -1;
    }
    int ret = pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);
    return (ret == 0) ? 0 : -1;
}

int sync_init(SharedData *data) {
    /* Per-word mutex'ler */
    for (int i = 0; i < data->total_words; i++) {
        if (init_shared_mutex(&data->words[i].word_mutex) != 0) {
            fprintf(stderr, "Error: Failed to init word mutex %d\n", i);
            return -1;
        }
    }

    /* Floor mutex ve condition variable'lar */
    for (int i = 0; i < data->config.num_floors; i++) {
        if (init_shared_mutex(&data->floors[i].floor_mutex) != 0) {
            fprintf(stderr, "Error: Failed to init floor mutex %d\n", i);
            return -1;
        }
        if (init_shared_cond(&data->floors[i].floor_cond) != 0) {
            fprintf(stderr, "Error: Failed to init floor cond %d\n", i);
            return -1;
        }
    }

    /* Delivery elevator senkronizasyonu */
    if (init_shared_mutex(&data->delivery_elevator.elev_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init delivery elevator mutex\n");
        return -1;
    }
    if (init_shared_cond(&data->delivery_elevator.elev_cond) != 0) {
        fprintf(stderr, "Error: Failed to init delivery elevator cond\n");
        return -1;
    }
    if (sem_init(&data->delivery_elevator.request_sem, 1, 0) != 0) {
        fprintf(stderr, "Error: Failed to init delivery elevator semaphore\n");
        return -1;
    }

    /* Reposition elevator senkronizasyonu */
    if (init_shared_mutex(&data->reposition_elevator.elev_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init reposition elevator mutex\n");
        return -1;
    }
    if (init_shared_cond(&data->reposition_elevator.elev_cond) != 0) {
        fprintf(stderr, "Error: Failed to init reposition elevator cond\n");
        return -1;
    }
    if (sem_init(&data->reposition_elevator.request_sem, 1, 0) != 0) {
        fprintf(stderr, "Error: Failed to init reposition elevator semaphore\n");
        return -1;
    }

    /* Round-robin mutex */
    if (init_shared_mutex(&data->round_robin_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init round-robin mutex\n");
        return -1;
    }

    /* Istatistik mutex */
    if (init_shared_mutex(&data->stats_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init stats mutex\n");
        return -1;
    }

    /* Global state mutex/condition variable */
    if (init_shared_mutex(&data->state_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init state mutex\n");
        return -1;
    }
    if (init_shared_cond(&data->state_cond) != 0) {
        fprintf(stderr, "Error: Failed to init state condition variable\n");
        return -1;
    }

    /* Children mutex */
    if (init_shared_mutex(&data->children_mutex) != 0) {
        fprintf(stderr, "Error: Failed to init children mutex\n");
        return -1;
    }

    return 0;
}

void sync_destroy(SharedData *data) {
    /* Per-word mutex'leri yok et */
    for (int i = 0; i < data->total_words; i++) {
        pthread_mutex_destroy(&data->words[i].word_mutex);
    }

    /* Floor mutex/cond yok et */
    for (int i = 0; i < data->config.num_floors; i++) {
        pthread_mutex_destroy(&data->floors[i].floor_mutex);
        pthread_cond_destroy(&data->floors[i].floor_cond);
    }

    /* Elevator senkronizasyonlarini yok et */
    pthread_mutex_destroy(&data->delivery_elevator.elev_mutex);
    pthread_cond_destroy(&data->delivery_elevator.elev_cond);
    sem_destroy(&data->delivery_elevator.request_sem);

    pthread_mutex_destroy(&data->reposition_elevator.elev_mutex);
    pthread_cond_destroy(&data->reposition_elevator.elev_cond);
    sem_destroy(&data->reposition_elevator.request_sem);

    /* Global mutex'leri yok et */
    pthread_mutex_destroy(&data->round_robin_mutex);
    pthread_mutex_destroy(&data->stats_mutex);
    pthread_mutex_destroy(&data->state_mutex);
    pthread_cond_destroy(&data->state_cond);
    pthread_mutex_destroy(&data->children_mutex);
}