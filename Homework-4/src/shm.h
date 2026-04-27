#ifndef SHM_H
#define SHM_H

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

/* ─── Sabitler ─────────────────────────────────────────────────────────── */
#define MAX_KEYWORDS   8
#define MAX_WORKERS    64
#define MAX_SOURCE_LEN 64
#define MAX_MSG_LEN    1024
#define MAX_TS_LEN     20
#define LEVEL_COUNT    4

/* Seviye indeksleri */
#define LVL_ERROR  0
#define LVL_WARN   1
#define LVL_INFO   2
#define LVL_DEBUG  3

extern const char* LEVEL_NAMES[LEVEL_COUNT];
extern const int   LEVEL_WEIGHTS[LEVEL_COUNT];

/* ─── Temel veri birimi ─────────────────────────────────────────────────── */
typedef struct {
    char timestamp[MAX_TS_LEN];
    int  level;        /* LVL_ERROR..LVL_DEBUG */
    char source[MAX_SOURCE_LEN];
    char message[MAX_MSG_LEN];
    int  is_eof;       /* 1: EOF sinyali, kayıt değil */
} log_entry_t;

/* ─── Sonuç yapısı (Region C) ───────────────────────────────────────────── */
typedef struct {
    char   level[8];
    long   total_entries;
    double total_weighted_score;
    double per_keyword_score[MAX_KEYWORDS];
    double per_thread_score[MAX_WORKERS];
    char   top_source[3][MAX_SOURCE_LEN];
    long   top_source_hits[3];
    int    ready;
} level_result_t;

/* ─── Bölge A ───────────────────────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t input_mutex;
    pthread_cond_t  not_full_a;
    pthread_cond_t  not_empty_a;
    int             eof_count_per_level[LEVEL_COUNT];
    int             total_readers;
    int             head, tail, count, capacity;
    log_entry_t     buf[]; /* flexible array */
} shm_region_a_t;

/* ─── Bölge B (×4) ──────────────────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t level_mutex;
    pthread_cond_t  not_full_b;
    pthread_cond_t  not_empty_b;
    int             eof_posted;
    int             head, tail, count, capacity;
    log_entry_t     buf[];
} shm_region_b_t;

/* ─── Bölge C ───────────────────────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t result_mutex;
    pthread_cond_t  result_cond;
    sem_t           level_sems[LEVEL_COUNT];
    level_result_t  results[LEVEL_COUNT];
} shm_region_c_t;

/* ─── Bölge D ───────────────────────────────────────────────────────────── */
typedef struct {
    pthread_mutex_t priority_mutex;
    pthread_cond_t  not_full_d;
    pthread_cond_t  not_empty_d;
    int             dispatcher_done;
    int             head, tail, count, capacity;
    log_entry_t     buf[];
} shm_region_d_t;

/* ─── Fonksiyon prototipleri ────────────────────────────────────────────── */
size_t shm_region_a_size(int capacity);
size_t shm_region_b_size(int capacity);
size_t shm_region_d_size(int capacity);

shm_region_a_t* shm_create_region_a(int capacity);
shm_region_b_t* shm_create_region_b(int capacity);
shm_region_c_t* shm_create_region_c(void);
shm_region_d_t* shm_create_region_d(int capacity);

void shm_destroy_region_a(shm_region_a_t* r);
void shm_destroy_region_b(shm_region_b_t* r, int capacity);
void shm_destroy_region_c(shm_region_c_t* r);
void shm_destroy_region_d(shm_region_d_t* r, int capacity);

/* Region A operasyonları */
void shm_a_push(shm_region_a_t* r, const log_entry_t* e);
int  shm_a_pop_timed(shm_region_a_t* r, log_entry_t* e, int timeout_sec);

/* Region B operasyonları */
void shm_b_push(shm_region_b_t* r, const log_entry_t* e);
int  shm_b_pop(shm_region_b_t* r, log_entry_t* e);

/* Region D operasyonları */
void shm_d_push(shm_region_d_t* r, const log_entry_t* e);
int  shm_d_pop(shm_region_d_t* r, log_entry_t* e);

#endif /* SHM_H */
