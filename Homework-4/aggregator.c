/*
 * aggregator.c - Aggregator process implementation
 *
 * Tasks performed by aggregator_process_main():
 *
 *   1. Spawn drain_region_d() thread to compute HIGH_PRIORITY_SCORE from Region D in parallel.
 *
 *   2. For each of the 4 log levels:
 *        - pthread_cond_timedwait on result_cond until results[i].ready;
 *        - sem_timedwait on level_sems[i] (redundant signal).
 *      Both have a T_TIMEOUT-second deadline; on timeout we just print a warning and continue.
 *
 *   3. Join the Region D drain thread to obtain HIGH_PRIORITY_SCORE.
 *
 *   4. Copy the four level_result_t into a local array, sort DESC by total_weighted_score, then call write_text() and write_binary().
 *
 * Output formatting follows the assignment rules:
 *   - .txt: right-aligned columns, two-space separators, .1f doubles
 *   - .bin: header uint32 magic 0xC5E3440B + version + counts + scores, followed by raw level_result_t structs; written to a .tmp file first, then atomically renamed.
*/

#include "aggregator.h"
#include "analyzer.h"           /* for count_overlapping() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>

/*
 * Binary header format
 * Little-endian everywhere (the only supported architecture for this assignment is x86_64). Keep the layout in sync with .bin readers and the tests' Python parser.
*/
typedef struct
{
    uint32_t magic;                  /* 0xC5E3440B - identifies our format */
    uint32_t version;                /* 1                                  */
    uint32_t num_levels;             /* always 4                           */
    uint32_t num_keywords;           /* equals -k count                    */
    double   total_weighted;
    double   high_priority_weighted;
} bin_header_t;

#define BIN_MAGIC   0xC5E3440Bu
#define BIN_VERSION 1u

/*
 * drain_region_d - thread that consumes Region D and accumulates HIGH_PRIORITY_SCORE.
 * Mirrors the Analyzer's keyword counting, but runs inside the Aggregator because Region D is independent of the per-level pipelines.*/
typedef struct
{
    shm_region_d_t* region_d;
    char** keywords;
    int n_keywords;
    double hp_score;        /* output written here */
} d_drain_arg_t;

static void* drain_region_d(void* arg)
{
    d_drain_arg_t* a = (d_drain_arg_t*)arg;

    log_entry_t entry;
    double score = 0.0;
    while (shm_d_pop(a->region_d, &entry))
    {
        for (int k = 0; k < a->n_keywords; k++)
        {
            int cnt = count_overlapping(entry.message, a->keywords[k]);
            score += cnt * LEVEL_WEIGHTS[entry.level];
        }
    }
    a->hp_score = score;
    return NULL;
}

/* sort_desc - selection sort by total_weighted_score, descending.
 * n is small (always 4), so selection sort is more than fast enough.
*/
static void sort_desc(level_result_t* res, int n)
{
    for (int i = 0; i < n - 1; i++)
    {
        int best = i;
        for (int j = i + 1; j < n; j++)
        {
            if (res[j].total_weighted_score > res[best].total_weighted_score)
                best = j;
        }
        if (best != i)
        {
            level_result_t tmp = res[i];
            res[i]    = res[best];
            res[best] = tmp;
        }
    }
}

/*
 * write_text - produce the human-readable .txt output
 * Layout:
 *   KEYWORD_LIST: ...
 *   FILES: N
 *   TOTAL_WEIGHTED_SCORE: X.X
 *   HIGH_PRIORITY_SCORE: X.X
 *   FILTER_FILE: path
 *   # Levels sorted by total_weighted_score DESC
 *     LEVEL  ENTRIES  WEIGHTED_SCORE   <kw1>   <kw2> ...
 *   # Top-3 sources per level
 *     LEVEL    src:N  src:N  src:N
 *   # Per-thread contributions (weighted score)
 *     LEVEL    thread_0:X.X  thread_1:X.X ...
 *
 * All floating point numbers use exactly one decimal place.
*/
static void write_text(const char* path, level_result_t* res, int n, char** kws, int nk, int n_files, double total_w, double hp_w, int n_workers, const char* filter_path)
{
    FILE* f = fopen(path, "w");
    if (!f)
    {
        perror("fopen output");
        return;
    }

    /* --- Metadata header lines. */
    fprintf(f, "KEYWORD_LIST: ");
    for (int k = 0; k < nk; k++)
        fprintf(f, "%s%s", kws[k], k < nk - 1 ? "," : "");

    fprintf(f, "\n");
    fprintf(f, "FILES: %d\n",                  n_files);
    fprintf(f, "TOTAL_WEIGHTED_SCORE: %.1f\n", total_w);
    fprintf(f, "HIGH_PRIORITY_SCORE: %.1f\n",  hp_w);
    if (filter_path)
        fprintf(f, "FILTER_FILE: %s\n", filter_path);

    fprintf(f, "# Levels sorted by total_weighted_score DESC\n");

    /* --- Column header line. Right-aligned, two-space separator. */
    fprintf(f, "%7s  %7s  %14s", "LEVEL", "ENTRIES", "WEIGHTED_SCORE");
    for (int k = 0; k < nk; k++)
        fprintf(f, "  %8s", kws[k]);

    fprintf(f, "\n");

    /* --- Data rows for each level. */
    for (int i = 0; i < n; i++)
    {
        fprintf(f, "%7s  %7ld  %14.1f", res[i].level, res[i].total_entries, res[i].total_weighted_score);
        for (int k = 0; k < nk; k++)
            fprintf(f, "  %8.1f", res[i].per_keyword_score[k]);
        fprintf(f, "\n");
    }

    /* --- Top-3 source block. */
    fprintf(f, "# Top-3 sources per level\n");
    for(int i = 0; i < n; i++)
    {
        fprintf(f, "%-7s", res[i].level);
        int any = 0;
        for (int t = 0; t < 3; t++)
        {
            if (res[i].top_source[t][0])
            {
                fprintf(f, "  %s:%ld", res[i].top_source[t], res[i].top_source_hits[t]);
                any = 1;
            }
        }
        if (!any)
            fprintf(f, "  -");
        fprintf(f, "\n");
    }

    /* --- Per-thread block. We always emit slots 0..n_workers-1, even if the value is zero (the assignment expects a fixed shape). */
    fprintf(f, "# Per-thread contributions (weighted score)\n");
    for (int i = 0; i < n; i++)
    {
        fprintf(f, "%-7s", res[i].level);
        for (int w = 0; w < n_workers && w < MAX_WORKERS; w++)
            fprintf(f, "  thread_%d:%.1f", w, res[i].per_thread_score[w]);
        fprintf(f, "\n");
    }
    fclose(f);
}

/*
 * write_binary - produce the binary .bin file atomically
 * Strategy: write to "<path>.tmp", flush + fsync, fclose, then rename.
 * rename(2) is atomic on POSIX, so a partial write can never appear at "<path>"; if the program crashes mid-write, the .tmp file is leftover but the previous .bin (if any) is intact.
*/
static void write_binary(const char* path, level_result_t* res, int n, int nk, double total_w, double hp_w)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE* f = fopen(tmp, "wb");
    if (!f)
    {
        perror("fopen binary");
        return;
    }

    bin_header_t hdr =
    {
        .magic                  = BIN_MAGIC,
        .version                = BIN_VERSION,
        .num_levels             = (uint32_t)n,
        .num_keywords           = (uint32_t)nk,
        .total_weighted         = total_w,
        .high_priority_weighted = hp_w,
    };
    if (fwrite(&hdr, sizeof(hdr), 1, f) != 1)
    {
        perror("fwrite hdr");
        fclose(f);
        unlink(tmp);
        return;
    }
    for (int i = 0; i < n; i++)
    {
        if (fwrite(&res[i], sizeof(level_result_t), 1, f) != 1)
        {
            perror("fwrite res");
            fclose(f);
            unlink(tmp);
            return;
        }
    }

    /* Force the data to disk before renaming, so the rename is durable. */
    fflush(f);
    if (fsync(fileno(f)) != 0 && errno != EINVAL)
    {
        /* fsync EINVAL is normal on some filesystems; ignore it. */
        perror("fsync (warning)");
    }
    fclose(f);

    if (rename(tmp, path) != 0)
    {
        perror("rename");
        unlink(tmp);
    }
}

/*
 * aggregator_process_main - entry point of the Aggregator child process
*/
void aggregator_process_main(aggregator_arg_t* a)
{
    printf("[PID:%d] Aggregator started. Waiting for %d levels...\n", getpid(), LEVEL_COUNT);
    fflush(stdout);

    /* --- Start the Region D drain thread. */
    d_drain_arg_t darg =
    {
        .region_d   = a->region_d,
        .keywords   = a->keywords,
        .n_keywords = a->n_keywords,
        .hp_score   = 0.0,
    };
    pthread_t d_thread;
    pthread_create(&d_thread, NULL, drain_region_d, &darg);

    /* --- Wait for each level's result with a T-second deadline. */
    for (int i = 0; i < LEVEL_COUNT; i++)
    {
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += a->timeout_sec;

        /* (1) cond_timedwait on result_cond until results[i].ready or
         *     the deadline expires. */
        pthread_mutex_lock(&a->region_c->result_mutex);
        while (!a->region_c->results[i].ready)
        {
            int rc = pthread_cond_timedwait(&a->region_c->result_cond, &a->region_c->result_mutex, &deadline);
            if (rc == ETIMEDOUT)
            {
                if (a->region_c->results[i].ready)
                    break;
                fprintf(stderr,
                        "[AGG] WARNING: timeout waiting for level %s (cond)\n",
                        LEVEL_NAMES[i]);
                break;
            }
            else if (rc != 0)
            {
                fprintf(stderr, "[AGG] cond_timedwait error: %s\n",strerror(rc));
                break;
            }
        }
        pthread_mutex_unlock(&a->region_c->result_mutex);

        /* (2) sem_timedwait - extra signal that the matching Analyzer used to mark its result ready. Posted exactly once per level by the reporting thread, so a second wait would hang; we use a separate timeout. */
        struct timespec ts2;
        clock_gettime(CLOCK_REALTIME, &ts2);
        ts2.tv_sec += a->timeout_sec;
        if (sem_timedwait(&a->region_c->level_sems[i], &ts2) != 0)
        {
            if (errno != ETIMEDOUT)
                fprintf(stderr, "[AGG] sem_timedwait %d: %s\n",
                        i, strerror(errno));
            /* Even on timeout we trust ready=1 from cond_timedwait. */
        }

        printf("[PID:%d] %s result received.\n", getpid(), LEVEL_NAMES[i]);
        fflush(stdout);
    }

    /* --- Wait for the Region D drain thread to finish. The drain thread blocks in shm_d_pop until the Dispatcher set dispatcher_done, which happens when the Dispatcher exits. */
    pthread_join(d_thread, NULL);

    /* --- Snapshot results, compute total, sort DESC. */
    level_result_t sorted[LEVEL_COUNT];
    memcpy(sorted, a->region_c->results, sizeof(sorted));

    double total_w = 0.0;
    for (int i = 0; i < LEVEL_COUNT; i++)
        total_w += sorted[i].total_weighted_score;

    sort_desc(sorted, LEVEL_COUNT);

    printf("[PID:%d] All results received. Writing output files...\n", getpid());
    fflush(stdout);

    /* --- Emit .txt and .bin. */
    write_text(a->output_path, sorted, LEVEL_COUNT,a->keywords, a->n_keywords, a->n_files, total_w, darg.hp_score, a->n_workers, a->filter_path);
    write_binary(a->binary_path, sorted, LEVEL_COUNT, a->n_keywords, total_w, darg.hp_score);

    printf("[PID:%d] Output files written: %s, %s\n", getpid(), a->output_path, a->binary_path);
    printf("[PID:%d] Aggregator exiting.\n", getpid());
    fflush(stdout);
    exit(EXIT_SUCCESS);
}