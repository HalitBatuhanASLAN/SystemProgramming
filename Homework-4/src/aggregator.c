#include "aggregator.h"
#include "analyzer.h"  /* count_overlapping */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

/* ─── Binary başlık ─────────────────────────────────────────────────────── */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t num_levels;
    uint32_t num_keywords;
    double   total_weighted;
    double   high_priority_weighted;
} bin_header_t;

#define BIN_MAGIC   0xC5E3440B
#define BIN_VERSION 1

/* ─── Region D tüketici thread ───────────────────────────────────────────── */
typedef struct {
    shm_region_d_t* region_d;
    char**          keywords;
    int             n_keywords;
    double          hp_score;   /* sonuç buraya yazılır */
} d_drain_arg_t;

static void* drain_region_d(void* arg) {
    d_drain_arg_t* a = (d_drain_arg_t*)arg;
    log_entry_t e;
    double score = 0.0;
    while (shm_d_pop(a->region_d, &e)) {
        for (int k = 0; k < a->n_keywords; k++) {
            int cnt = count_overlapping(e.message, a->keywords[k]);
            score += cnt * LEVEL_WEIGHTS[e.level];
        }
    }
    a->hp_score = score;
    return NULL;
}

/* ─── Sıralama (AZALAN) ─────────────────────────────────────────────────── */
static void sort_desc(level_result_t* res, int n) {
    for (int i = 0; i < n-1; i++) {
        int best = i;
        for (int j = i+1; j < n; j++)
            if (res[j].total_weighted_score > res[best].total_weighted_score)
                best = j;
        if (best != i) {
            level_result_t tmp = res[i]; res[i] = res[best]; res[best] = tmp;
        }
    }
}

/* ─── Metin çıktı ───────────────────────────────────────────────────────── */
static void write_text(const char* path, level_result_t* res, int n,
                       char** kws, int nk, int n_files,
                       double total_w, double hp_w) {
    FILE* f = fopen(path, "w");
    if (!f) { perror("fopen output"); return; }

    fprintf(f, "KEYWORD_LIST: ");
    for (int k = 0; k < nk; k++)
        fprintf(f, "%s%s", kws[k], k < nk-1 ? "," : "");
    fprintf(f, "\n");
    fprintf(f, "FILES: %d\n", n_files);
    fprintf(f, "TOTAL_WEIGHTED_SCORE: %.1f\n", total_w);
    fprintf(f, "HIGH_PRIORITY_SCORE: %.1f\n", hp_w);
    fprintf(f, "# Levels sorted by total_weighted_score DESC\n");

    /* Başlık satırı */
    fprintf(f, "%-7s %8s %15s", "LEVEL","ENTRIES","WEIGHTED_SCORE");
    for (int k = 0; k < nk; k++) fprintf(f, "  %8s", kws[k]);
    fprintf(f, "\n");

    for (int i = 0; i < n; i++) {
        fprintf(f, "%-7s %8ld %15.1f",
                res[i].level, res[i].total_entries, res[i].total_weighted_score);
        for (int k = 0; k < nk; k++)
            fprintf(f, "  %8.1f", res[i].per_keyword_score[k]);
        fprintf(f, "\n");
    }

    fprintf(f, "# Top-3 sources per level\n");
    for (int i = 0; i < n; i++) {
        fprintf(f, "%-7s", res[i].level);
        for (int t = 0; t < 3; t++)
            if (res[i].top_source[t][0])
                fprintf(f, "  %s:%ld", res[i].top_source[t], res[i].top_source_hits[t]);
        fprintf(f, "\n");
    }

    fprintf(f, "# Per-thread contributions (weighted score)\n");
    for (int i = 0; i < n; i++) {
        fprintf(f, "%-7s", res[i].level);
        for (int w = 0; w < MAX_WORKERS; w++) {
            if (res[i].per_thread_score[w] == 0.0) break;
            fprintf(f, "  thread_%d:%.1f", w, res[i].per_thread_score[w]);
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

/* ─── Binary çıktı (atomik rename) ─────────────────────────────────────── */
static void write_binary(const char* path, level_result_t* res, int n,
                         int nk, double total_w, double hp_w) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) { perror("fopen binary"); return; }

    bin_header_t hdr = {
        .magic                  = BIN_MAGIC,
        .version                = BIN_VERSION,
        .num_levels             = (uint32_t)n,
        .num_keywords           = (uint32_t)nk,
        .total_weighted         = total_w,
        .high_priority_weighted = hp_w,
    };
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
        { perror("fwrite hdr"); fclose(f); return; }
    for (int i = 0; i < n; i++)
        if (fwrite(&res[i], sizeof(level_result_t), 1, f) != 1)
            { perror("fwrite res"); fclose(f); return; }
    fclose(f);
    if (rename(tmp, path) != 0) perror("rename");
}

/* ─── Aggregator Process Main ────────────────────────────────────────────── */
void aggregator_process_main(aggregator_arg_t* a) {
    printf("[PID:%d] Aggregator started. Waiting for %d levels...\n",
           getpid(), LEVEL_COUNT);
    fflush(stdout);

    /* Region D'yi arka planda ayrı thread ile tüket –
       böylece Dispatcher bloke olmaz */
    d_drain_arg_t darg = {
        .region_d  = a->region_d,
        .keywords  = a->keywords,
        .n_keywords= a->n_keywords,
        .hp_score  = 0.0,
    };
    pthread_t d_thread;
    pthread_create(&d_thread, NULL, drain_region_d, &darg);

    /* Her seviye için sem_timedwait */
    for (int i = 0; i < LEVEL_COUNT; i++) {
        /* Zaten ready ise sem değeri > 0, anında döner */
        while (1) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += a->timeout_sec;
            int rc = sem_timedwait(&a->region_c->level_sems[i], &ts);
            if (rc == 0) break;
            if (errno == ETIMEDOUT) {
                if (a->region_c->results[i].ready) break;
                /* Yoksa bir daha dene */
            } else {
                fprintf(stderr, "[AGG] sem_timedwait %d: %s\n",
                        i, strerror(errno));
                break;
            }
        }
        printf("[PID:%d] %s result received.\n",
               getpid(), LEVEL_NAMES[i]);
        fflush(stdout);
    }

    /* D thread'ini bekle */
    pthread_join(d_thread, NULL);

    /* Sonuçları kopyala + sırala */
    level_result_t sorted[LEVEL_COUNT];
    memcpy(sorted, a->region_c->results, sizeof(sorted));

    double total_w = 0.0;
    for (int i = 0; i < LEVEL_COUNT; i++)
        total_w += sorted[i].total_weighted_score;

    sort_desc(sorted, LEVEL_COUNT);

    printf("[PID:%d] All results received. Writing output files...\n", getpid());
    fflush(stdout);

    write_text(a->output_path, sorted, LEVEL_COUNT,
               a->keywords, a->n_keywords, a->n_files, total_w, darg.hp_score);
    write_binary(a->binary_path, sorted, LEVEL_COUNT,
                 a->n_keywords, total_w, darg.hp_score);

    printf("[PID:%d] Output files written: %s, %s\n",
           getpid(), a->output_path, a->binary_path);
    printf("[PID:%d] Aggregator exiting.\n", getpid());
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
