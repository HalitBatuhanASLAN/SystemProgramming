#include "analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <errno.h>

/* ─── Overlapping substring sayımı ─────────────────────────────────────── */
int count_overlapping(const char* text, const char* keyword) {
    int count = 0;
    int klen  = (int)strlen(keyword);
    int tlen  = (int)strlen(text);
    if (klen == 0 || klen > tlen) return 0;
    for (int i = 0; i <= tlen - klen; i++)
        if (memcmp(text + i, keyword, klen) == 0) count++;
    return count;
}

/* ─── Per-source harita ─────────────────────────────────────────────────── */
#define MAX_SOURCES 512
typedef struct { char name[MAX_SOURCE_LEN]; long hits; } src_entry_t;
typedef struct { src_entry_t e[MAX_SOURCES]; int n; } src_map_t;

static void srcmap_add(src_map_t* m, const char* src) {
    for (int i = 0; i < m->n; i++) {
        if (strcmp(m->e[i].name, src) == 0) { m->e[i].hits++; return; }
    }
    if (m->n < MAX_SOURCES) {
        snprintf(m->e[m->n].name, MAX_SOURCE_LEN, "%s", src);
        m->e[m->n].hits = 1;
        m->n++;
    }
}

/* ─── Paylaşılan süreç-içi durum ────────────────────────────────────────── */
/* Bu değişkenler her fork sonrası child'a ait ayrı kopya olur */
static int               g_level_idx;
static int               g_n_keywords;
static char**            g_keywords;
static shm_region_b_t*   g_region_b;
static shm_region_c_t*   g_region_c;

/* Barrier: tüm worker'lar bittikten sonra raporlama yapılır */
static pthread_barrier_t g_barrier;

/* TLS key */
static pthread_key_t     g_tls_key;

/* Worker istatistikleri – mutex korumalı */
static pthread_mutex_t   g_agg_mu = PTHREAD_MUTEX_INITIALIZER;
static long              g_total_entries  = 0;
static double            g_total_weighted = 0.0;
static src_map_t         g_src_map;

/* TID toplama – barrier'dan önce doldurulur */
static pthread_mutex_t   g_tid_mu = PTHREAD_MUTEX_INITIALIZER;
static pid_t             g_tids[MAX_WORKERS];
static int               g_tid_count = 0;

/* Destructor sayacı (atomik) */
static int               g_dest_idx = 0;

/* ─── TLS Destructor ────────────────────────────────────────────────────── */
static void tls_destructor(void* val) {
    if (!val) return;
    double* scores = (double*)val;

    int my_idx = __sync_fetch_and_add(&g_dest_idx, 1);

    pthread_mutex_lock(&g_region_c->result_mutex);
    level_result_t* res = &g_region_c->results[g_level_idx];
    for (int k = 0; k < g_n_keywords; k++) {
        res->per_keyword_score[k] += scores[k];
    }
    if (my_idx < MAX_WORKERS) {
        double thread_total = 0.0;
        for (int k = 0; k < g_n_keywords; k++) thread_total += scores[k];
        res->per_thread_score[my_idx] = thread_total;
    }
    pthread_mutex_unlock(&g_region_c->result_mutex);

    free(scores);
}

/* ─── Worker Thread ─────────────────────────────────────────────────────── */
typedef struct { int worker_idx; } worker_arg_t;

static void* worker_thread_func(void* arg) {
    worker_arg_t* wa   = (worker_arg_t*)arg;
    int           widx = wa->worker_idx;

    /* Sistem TID'ini kaydet – mutex korumalı */
    pid_t my_tid = (pid_t)syscall(SYS_gettid);
    pthread_mutex_lock(&g_tid_mu);
    g_tids[g_tid_count++] = my_tid;
    pthread_mutex_unlock(&g_tid_mu);

    printf("[PID:%d][TID:%d] Worker %d started.\n",
           getpid(), (int)my_tid, widx);
    fflush(stdout);

    /* TLS başlat */
    double* tls = calloc(g_n_keywords, sizeof(double));
    if (!tls) { perror("calloc tls"); return NULL; }
    pthread_setspecific(g_tls_key, tls);

    /* Lokal istatistikler */
    src_map_t   local_src;  memset(&local_src, 0, sizeof(local_src));
    long        local_cnt    = 0;
    double      local_weighted = 0.0;

    /* ── Kayıt tüketme döngüsü ── */
    log_entry_t entry;
    while (shm_b_pop(g_region_b, &entry)) {
        local_cnt++;
        for (int k = 0; k < g_n_keywords; k++) {
            int hits = count_overlapping(entry.message, g_keywords[k]);
            double score = hits * LEVEL_WEIGHTS[g_level_idx];
            tls[k]         += score;
            local_weighted += score;
        }
        srcmap_add(&local_src, entry.source);
    }

    /* Global istatistiklere ekle */
    pthread_mutex_lock(&g_agg_mu);
    g_total_entries  += local_cnt;
    g_total_weighted += local_weighted;
    for (int i = 0; i < local_src.n; i++) {
        for (int j = 0; j < local_src.e[i].hits; j++)
            srcmap_add(&g_src_map, local_src.e[i].name);
    }
    pthread_mutex_unlock(&g_agg_mu);

    printf("[PID:%d][TID:%d] Worker %d done. Entries: %ld, Weighted: %.1f\n",
           getpid(), (int)my_tid, widx, local_cnt, local_weighted);
    fflush(stdout);

    /* ── Barrier: tüm worker'lar buluşuyor ── */
    pthread_barrier_wait(&g_barrier);

    /* Barrier sonrası: min TID'i bul (tüm TID'ler artık g_tids'te) */
    pid_t min_tid = g_tids[0];
    pthread_mutex_lock(&g_tid_mu);
    for (int i = 1; i < g_tid_count; i++)
        if (g_tids[i] < min_tid) min_tid = g_tids[i];
    pthread_mutex_unlock(&g_tid_mu);

    if (my_tid == min_tid) {
        printf("[PID:%d][TID:%d] ** Reporting thread (lowest TID). Level: %s **\n",
               getpid(), (int)my_tid, LEVEL_NAMES[g_level_idx]);
        fflush(stdout);

        /* Region C'ye özet yaz */
        pthread_mutex_lock(&g_region_c->result_mutex);
        level_result_t* res = &g_region_c->results[g_level_idx];
        res->total_entries        = g_total_entries;
        res->total_weighted_score = g_total_weighted;

        /* Top-3 source – seçim sıralaması */
        src_map_t* sm = &g_src_map;
        int top_n = sm->n < 3 ? sm->n : 3;
        for (int t = 0; t < top_n; t++) {
            int best = t;
            for (int j = t+1; j < sm->n; j++)
                if (sm->e[j].hits > sm->e[best].hits) best = j;
            src_entry_t tmp = sm->e[t]; sm->e[t] = sm->e[best]; sm->e[best] = tmp;
            snprintf(res->top_source[t], MAX_SOURCE_LEN, "%s", sm->e[t].name);
            res->top_source_hits[t] = sm->e[t].hits;
        }

        res->ready = 1;
        pthread_cond_broadcast(&g_region_c->result_cond);
        pthread_mutex_unlock(&g_region_c->result_mutex);

        /* Semaphore post */
        sem_post(&g_region_c->level_sems[g_level_idx]);

        printf("[PID:%d][TID:%d] Total entries: %ld | Weighted score: %.1f\n",
               getpid(), (int)my_tid, g_total_entries, g_total_weighted);
        fflush(stdout);
    }

    /* return NULL → TLS destructor otomatik tetiklenir */
    return NULL;
}

/* ─── Analyzer Process Main ─────────────────────────────────────────────── */
void analyzer_process_main(analyzer_arg_t* a) {
    /* Global state – bu child'a özel kopya */
    g_level_idx  = a->level_idx;
    g_n_keywords = a->n_keywords;
    g_keywords   = a->keywords;
    g_region_b   = a->region_b;
    g_region_c   = a->region_c;
    g_tid_count  = 0;
    g_dest_idx   = 0;
    g_total_entries  = 0;
    g_total_weighted = 0.0;
    memset(&g_src_map, 0, sizeof(g_src_map));

    /* mutex'leri sıfırla – fork sonrası çocukta yeniden başlatmak güvenli */
    pthread_mutex_init(&g_agg_mu, NULL);
    pthread_mutex_init(&g_tid_mu, NULL);

    printf("[PID:%d] Analyzer %s started. Workers: %d\n",
           getpid(), LEVEL_NAMES[a->level_idx], a->n_workers);
    fflush(stdout);

    pthread_key_create(&g_tls_key, tls_destructor);
    pthread_barrier_init(&g_barrier, NULL, (unsigned)a->n_workers);

    pthread_t*    tids  = calloc(a->n_workers, sizeof(pthread_t));
    worker_arg_t* wargs = calloc(a->n_workers, sizeof(worker_arg_t));

    for (int w = 0; w < a->n_workers; w++) {
        wargs[w].worker_idx = w;
        if (pthread_create(&tids[w], NULL, worker_thread_func, &wargs[w]) != 0) {
            perror("pthread_create worker"); exit(EXIT_FAILURE);
        }
    }

    for (int w = 0; w < a->n_workers; w++)
        pthread_join(tids[w], NULL);

    pthread_barrier_destroy(&g_barrier);
    pthread_key_delete(g_tls_key);
    pthread_mutex_destroy(&g_agg_mu);
    pthread_mutex_destroy(&g_tid_mu);
    free(tids);
    free(wargs);

    printf("[PID:%d] Analyzer %s exiting.\n",
           getpid(), LEVEL_NAMES[a->level_idx]);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
