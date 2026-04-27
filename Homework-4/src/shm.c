#include "shm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>

const char* LEVEL_NAMES[LEVEL_COUNT]   = {"ERROR","WARN","INFO","DEBUG"};
const int   LEVEL_WEIGHTS[LEVEL_COUNT] = {4, 3, 2, 1};

/* ─── Boyut hesaplamaları ─────────────────────────────────────────────── */
size_t shm_region_a_size(int capacity) {
    return sizeof(shm_region_a_t) + capacity * sizeof(log_entry_t);
}
size_t shm_region_b_size(int capacity) {
    return sizeof(shm_region_b_t) + capacity * sizeof(log_entry_t);
}
size_t shm_region_d_size(int capacity) {
    return sizeof(shm_region_d_t) + capacity * sizeof(log_entry_t);
}

/* ─── Primitive başlatma yardımcıları ───────────────────────────────────── */
static void init_shared_mutex(pthread_mutex_t* m) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if (pthread_mutex_init(m, &a) != 0) { perror("mutex_init"); exit(1); }
    pthread_mutexattr_destroy(&a);
}
static void init_shared_cond(pthread_cond_t* c) {
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if (pthread_cond_init(c, &a) != 0) { perror("cond_init"); exit(1); }
    pthread_condattr_destroy(&a);
}
static void* alloc_shared(size_t sz) {
    void* p = mmap(NULL, sz, PROT_READ|PROT_WRITE,
                   MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    memset(p, 0, sz);
    return p;
}

/* ─── Oluşturma ─────────────────────────────────────────────────────────── */
shm_region_a_t* shm_create_region_a(int capacity) {
    shm_region_a_t* r = alloc_shared(shm_region_a_size(capacity));
    init_shared_mutex(&r->input_mutex);
    init_shared_cond(&r->not_full_a);
    init_shared_cond(&r->not_empty_a);
    r->capacity = capacity;
    return r;
}
shm_region_b_t* shm_create_region_b(int capacity) {
    shm_region_b_t* r = alloc_shared(shm_region_b_size(capacity));
    init_shared_mutex(&r->level_mutex);
    init_shared_cond(&r->not_full_b);
    init_shared_cond(&r->not_empty_b);
    r->capacity = capacity;
    return r;
}
shm_region_c_t* shm_create_region_c(void) {
    shm_region_c_t* r = alloc_shared(sizeof(shm_region_c_t));
    init_shared_mutex(&r->result_mutex);
    init_shared_cond(&r->result_cond);
    for (int i = 0; i < LEVEL_COUNT; i++) {
        if (sem_init(&r->level_sems[i], 1, 0) != 0) {
            perror("sem_init"); exit(1);
        }
        strncpy(r->results[i].level, LEVEL_NAMES[i], 7);
    }
    return r;
}
shm_region_d_t* shm_create_region_d(int capacity) {
    shm_region_d_t* r = alloc_shared(shm_region_d_size(capacity));
    init_shared_mutex(&r->priority_mutex);
    init_shared_cond(&r->not_full_d);
    init_shared_cond(&r->not_empty_d);
    r->capacity = capacity;
    return r;
}

/* ─── Yıkım ─────────────────────────────────────────────────────────────── */
void shm_destroy_region_a(shm_region_a_t* r) {
    int cap = r->capacity;
    pthread_mutex_destroy(&r->input_mutex);
    pthread_cond_destroy(&r->not_full_a);
    pthread_cond_destroy(&r->not_empty_a);
    munmap(r, shm_region_a_size(cap));
}
void shm_destroy_region_b(shm_region_b_t* r, int capacity) {
    pthread_mutex_destroy(&r->level_mutex);
    pthread_cond_destroy(&r->not_full_b);
    pthread_cond_destroy(&r->not_empty_b);
    munmap(r, shm_region_b_size(capacity));
}
void shm_destroy_region_c(shm_region_c_t* r) {
    pthread_mutex_destroy(&r->result_mutex);
    pthread_cond_destroy(&r->result_cond);
    for (int i = 0; i < LEVEL_COUNT; i++) sem_destroy(&r->level_sems[i]);
    munmap(r, sizeof(shm_region_c_t));
}
void shm_destroy_region_d(shm_region_d_t* r, int capacity) {
    pthread_mutex_destroy(&r->priority_mutex);
    pthread_cond_destroy(&r->not_full_d);
    pthread_cond_destroy(&r->not_empty_d);
    munmap(r, shm_region_d_size(capacity));
}

/* ─── Yardımcı: tüm EOF'lar geldi mi? (mutex tutularak çağrılır) ───────── */
static int all_eof_done(shm_region_a_t* r) {
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (r->eof_count_per_level[i] < r->total_readers) return 0;
    return 1;
}

/* ─── Region A ─────────────────────────────────────────────────────────── */
void shm_a_push(shm_region_a_t* r, const log_entry_t* e) {
    pthread_mutex_lock(&r->input_mutex);
    while (r->count == r->capacity)
        pthread_cond_wait(&r->not_full_a, &r->input_mutex);
    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;
    if (e->is_eof) r->eof_count_per_level[e->level]++;
    /* broadcast: hem veri bekleyeni hem all_done bekleyeni uyandır */
    pthread_cond_broadcast(&r->not_empty_a);
    pthread_mutex_unlock(&r->input_mutex);
}

/*
 * Döner: 1 = geçerli kayıt alındı (e dolduruldu)
 *         0 = tüm EOF'lar geldi VE buffer boş → Dispatcher çıkabilir
 */
int shm_a_pop_timed(shm_region_a_t* r, log_entry_t* e, int timeout_sec) {
    pthread_mutex_lock(&r->input_mutex);

    while (1) {
        /* Veri var mı? */
        if (r->count > 0) {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_a);
            pthread_mutex_unlock(&r->input_mutex);
            return 1;
        }
        /* Buffer boş – tüm EOF'lar tamamlandı mı? */
        if (all_eof_done(r)) {
            pthread_mutex_unlock(&r->input_mutex);
            return 0;
        }
        /* Bekle: veri veya yeni EOF gelince broadcast ile uyanacağız */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_sec;
        int rc = pthread_cond_timedwait(&r->not_empty_a, &r->input_mutex, &ts);
        if (rc == ETIMEDOUT) {
            /* Timeout: yeniden all_done kontrol et */
            if (all_eof_done(r) && r->count == 0) {
                pthread_mutex_unlock(&r->input_mutex);
                return 0;
            }
            /* Henüz değil, tekrar bekle */
        }
        /* EINTR veya spurious wakeup: döngü devam eder */
    }
}

/* ─── Region B ─────────────────────────────────────────────────────────── */
void shm_b_push(shm_region_b_t* r, const log_entry_t* e) {
    pthread_mutex_lock(&r->level_mutex);
    while (r->count == r->capacity)
        pthread_cond_wait(&r->not_full_b, &r->level_mutex);
    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;
    pthread_cond_broadcast(&r->not_empty_b);
    pthread_mutex_unlock(&r->level_mutex);
}

int shm_b_pop(shm_region_b_t* r, log_entry_t* e) {
    pthread_mutex_lock(&r->level_mutex);
    while (1) {
        if (r->count > 0) {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_b);
            pthread_mutex_unlock(&r->level_mutex);
            return 1;
        }
        if (r->eof_posted) {
            pthread_mutex_unlock(&r->level_mutex);
            return 0;
        }
        pthread_cond_wait(&r->not_empty_b, &r->level_mutex);
    }
}

/* ─── Region D ─────────────────────────────────────────────────────────── */
void shm_d_push(shm_region_d_t* r, const log_entry_t* e) {
    pthread_mutex_lock(&r->priority_mutex);
    while (r->count == r->capacity)
        pthread_cond_wait(&r->not_full_d, &r->priority_mutex);
    r->buf[r->tail] = *e;
    r->tail = (r->tail + 1) % r->capacity;
    r->count++;
    pthread_cond_signal(&r->not_empty_d);
    pthread_mutex_unlock(&r->priority_mutex);
}

int shm_d_pop(shm_region_d_t* r, log_entry_t* e) {
    pthread_mutex_lock(&r->priority_mutex);
    while (1) {
        if (r->count > 0) {
            *e = r->buf[r->head];
            r->head = (r->head + 1) % r->capacity;
            r->count--;
            pthread_cond_signal(&r->not_full_d);
            pthread_mutex_unlock(&r->priority_mutex);
            return 1;
        }
        if (r->dispatcher_done) {
            pthread_mutex_unlock(&r->priority_mutex);
            return 0;
        }
        pthread_cond_wait(&r->not_empty_d, &r->priority_mutex);
    }
}
