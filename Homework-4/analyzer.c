/* analyzer.c - Analyzer process implementation
 * Worker lifecycle:
 *
 *   1. Each worker thread calls syscall(SYS_gettid) to get its kernel
 *      thread id, registers it in g_tids[], and creates a TLS struct
 *      that holds per-keyword scores.
 *
 *   2. Each worker pops entries from Region B in a loop. For every
 *      entry: for every keyword: count_overlapping() * LEVEL_WEIGHT
 *      gets accumulated into the worker's TLS scores AND into a shared
 *      global tally (under g_agg_mu).
 *
 *   3. Once Region B is exhausted, every worker hits a pthread_barrier.
 *      This guarantees that all g_tids[] entries have been registered
 *      before anyone tries to read them.
 *
 *   4. The thread with the lowest TID becomes the "reporting thread".
 *      Other workers simply return; their TLS destructor will flush
 *      per-thread scores to Region C under the result_mutex.
 *
 *   5. The reporting thread waits for every other worker's destructor
 *      to run (via g_destr_cv), then writes the final summary fields
 *      (total_entries, total_weighted_score, top-3 sources) and sets
 *      ready=1, broadcasts result_cond, and posts level_sems[level_idx].
 *
 * The reporting thread also flushes its OWN TLS data manually (because
 * it doesn't return until after the publish step) and clears its TLS
 * pointer so the destructor doesn't double-flush at thread exit.*/

#include "analyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <errno.h>

/* =============================================================================
 * count_overlapping - sliding-window substring counter.
 * For "aaaa" / "aa" this returns 3 (matches at positions 0, 1, 2).
 * ============================================================================= */
int count_overlapping(const char* text, const char* keyword)
{
    int count = 0;
    int klen  = (int)strlen(keyword);
    int tlen  = (int)strlen(text);
    if (klen == 0 || klen > tlen)
    {
        return 0;
    }
    for (int i = 0; i <= tlen - klen; i++)
    {
        if (memcmp(text + i, keyword, klen) == 0)
        {
            count++;
        }
    }
    return count;
}

/* =============================================================================
 * src_map_t - small associative array (source name -> hit count)
 * Used for top-3 source computation. Linear in the number of distinct
 * sources, which stays well under MAX_SOURCES in practice.
 * ============================================================================= */
#define MAX_SOURCES 512

typedef struct
{
    char name[MAX_SOURCE_LEN];
    long hits;
} src_entry_t;

typedef struct
{
    src_entry_t e[MAX_SOURCES];
    int         n;
} src_map_t;

static void srcmap_inc(src_map_t* m, const char* src, long delta)
{
    for (int i = 0; i < m->n; i++)
    {
        if (strcmp(m->e[i].name, src) == 0)
        {
            m->e[i].hits += delta;
            return;
        }
    }
    if (m->n < MAX_SOURCES)
    {
        snprintf(m->e[m->n].name, MAX_SOURCE_LEN, "%s", src);
        m->e[m->n].hits = delta;
        m->n++;
    }
}

/* =============================================================================
 * Process-wide state (one set per Analyzer process)
 * Set in analyzer_process_main() before any thread is created.
 * ============================================================================= */
static int               g_level_idx;
static int               g_n_keywords;
static int               g_n_workers;
static char**            g_keywords;
static shm_region_b_t*   g_region_b;
static shm_region_c_t*   g_region_c;

/* Barrier hit by every worker after it finishes consuming Region B.
 * Past this point every g_tids[i] has been written, so the lowest-TID
 * election is well-defined. */
static pthread_barrier_t g_barrier;

/* TLS key whose destructor flushes per-thread scores into Region C. */
static pthread_key_t     g_tls_key;

/* Aggregated worker stats (under g_agg_mu). */
static pthread_mutex_t   g_agg_mu = PTHREAD_MUTEX_INITIALIZER;
static long              g_total_entries  = 0;
static double            g_total_weighted = 0.0;
static src_map_t         g_src_map;

/* TID registration (under g_tid_mu). */
static pthread_mutex_t   g_tid_mu = PTHREAD_MUTEX_INITIALIZER;
static pid_t             g_tids[MAX_WORKERS];
static int               g_tid_count = 0;

/* Tracking how many TLS destructors have already run. The reporting
 * thread waits on g_destr_cv until g_destr_done == n_workers - 1. */
static pthread_mutex_t   g_destr_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t    g_destr_cv = PTHREAD_COND_INITIALIZER;
static int               g_destr_done = 0;

/*
 * tls_data_t - the value stored under g_tls_key
 * scores: per-keyword tally for this worker
 * worker_idx: needed by the destructor to write per_thread_score[w]*/
typedef struct
{
    double* scores;
    int     worker_idx;
} tls_data_t;

/*
 * tls_destructor - automatically called by pthreads when a worker thread exits, with the value previously set via pthread_setspecific.
 *
 * Flushes per-keyword scores to Region C, fills per_thread_score[idx], frees the TLS struct, then increments g_destr_done so the reporting thread can proceed.*/
static void tls_destructor(void* val)
{
    if (!val)
        return;
    tls_data_t* td = (tls_data_t*)val;

    /* Flush this worker's contribution to Region C under the shared lock. */
    pthread_mutex_lock(&g_region_c->result_mutex);
    level_result_t* res = &g_region_c->results[g_level_idx];
    double thread_total = 0.0;
    for (int k = 0; k < g_n_keywords; k++)
    {
        res->per_keyword_score[k] += td->scores[k];
        thread_total              += td->scores[k];
    }
    if (td->worker_idx >= 0 && td->worker_idx < MAX_WORKERS)
        res->per_thread_score[td->worker_idx] = thread_total;
    pthread_mutex_unlock(&g_region_c->result_mutex);

    free(td->scores);
    free(td);

    /* Notify the reporting thread that one more flush has happened. */
    pthread_mutex_lock(&g_destr_mu);
    g_destr_done++;
    pthread_cond_broadcast(&g_destr_cv);
    pthread_mutex_unlock(&g_destr_mu);
}

/*
 * worker_thread_func - one worker thread inside the Analyzer*/
typedef struct
{
    int worker_idx;
} worker_arg_t;

static void* worker_thread_func(void* arg)
{
    worker_arg_t* wa   = (worker_arg_t*)arg;
    int           widx = wa->worker_idx;

    /* --- Register kernel TID (assignment requires syscall(SYS_gettid)). */
    pid_t my_tid = (pid_t)syscall(SYS_gettid);
    pthread_mutex_lock(&g_tid_mu);
    if (g_tid_count < MAX_WORKERS)
        g_tids[g_tid_count++] = my_tid;
    pthread_mutex_unlock(&g_tid_mu);

    printf("[PID:%d][TID:%d] Worker %d started.\n",getpid(), (int)my_tid, widx);
    fflush(stdout);

    /* --- Allocate the TLS struct and bind it to g_tls_key. */
    tls_data_t* td = calloc(1, sizeof(*td));
    if (!td)
    {
        perror("calloc tls");
        return NULL;
    }
    td->scores     = calloc(g_n_keywords, sizeof(double));
    td->worker_idx = widx;
    if (!td->scores)
    {
        perror("calloc tls.scores");
        free(td);
        return NULL;
    }
    pthread_setspecific(g_tls_key, td);

    /* --- Local stats; merged into globals at the end. */
    src_map_t local_src;
    memset(&local_src, 0, sizeof(local_src));
    long   local_cnt      = 0;
    double local_weighted = 0.0;

    /* --- Main consumption loop: pop from Region B, count keywords. */
    log_entry_t entry;
    while (shm_b_pop(g_region_b, &entry))
    {
        local_cnt++;
        for (int k = 0; k < g_n_keywords; k++)
        {
            int    hits  = count_overlapping(entry.message, g_keywords[k]);
            double score = hits * LEVEL_WEIGHTS[g_level_idx];
            td->scores[k]   += score;
            local_weighted  += score;
        }
        srcmap_inc(&local_src, entry.source, 1);
    }

    /* Merge local stats into the process-global aggregates. */
    pthread_mutex_lock(&g_agg_mu);
    g_total_entries  += local_cnt;
    g_total_weighted += local_weighted;
    for (int i = 0; i < local_src.n; i++)
        srcmap_inc(&g_src_map, local_src.e[i].name, local_src.e[i].hits);
    pthread_mutex_unlock(&g_agg_mu);

    printf("[PID:%d][TID:%d] Worker %d done. Entries: %ld, Weighted: %.1f\n",getpid(), (int)my_tid, widx, local_cnt, local_weighted);
    fflush(stdout);

    /* Barrier: every worker is here, so g_tids[] is fully populated. */
    pthread_barrier_wait(&g_barrier);

    /* Find the lowest TID; that worker becomes the reporting thread. */
    pid_t min_tid = g_tids[0];
    for (int i = 1; i < g_tid_count; i++)
    {
        if (g_tids[i] < min_tid)
            min_tid = g_tids[i];
    }
    int am_reporting = (my_tid == min_tid);

    /* --- Non-reporting threads return immediately; the TLS destructor fires automatically on thread exit and flushes their data. */
    if (!am_reporting)
        return NULL;

    /* Reporting thread path 
     * Wait for every other worker's TLS destructor to finish before publishing the level's summary. expected = n_workers - 1 because we (the reporting thread) flush our own data manually below. */
    int expected_destr = g_n_workers - 1;
    pthread_mutex_lock(&g_destr_mu);
    while (g_destr_done < expected_destr)
        pthread_cond_wait(&g_destr_cv, &g_destr_mu);
    pthread_mutex_unlock(&g_destr_mu);

    printf("[PID:%d][TID:%d] ** Reporting thread (lowest TID). Level: %s **\n", getpid(), (int)my_tid, LEVEL_NAMES[g_level_idx]);
    fflush(stdout);

    pthread_mutex_lock(&g_region_c->result_mutex);
    level_result_t* res = &g_region_c->results[g_level_idx];

    /* --- Flush our own TLS data manually (the destructor would fire too late, after we have already published ready=1). */
    if (td)
    {
        double my_total = 0.0;
        for (int k = 0; k < g_n_keywords; k++)
        {
            res->per_keyword_score[k] += td->scores[k];
            my_total += td->scores[k];
        }
        if (widx >= 0 && widx < MAX_WORKERS)
            res->per_thread_score[widx] = my_total;
        free(td->scores);
        free(td);
        /* Clear the TLS pointer so the destructor does NOT double-flush. */
        pthread_setspecific(g_tls_key, NULL);
    }

    /* --- Fill the summary fields. */
    res->total_entries        = g_total_entries;
    res->total_weighted_score = g_total_weighted;

    /* --- Compute top-3 sources by selecting max three times. */
    src_map_t* sm = &g_src_map;
    int top_n = sm->n < 3 ? sm->n : 3;
    for (int t = 0; t < top_n; t++)
    {
        int best = t;
        for (int j = t + 1; j < sm->n; j++)
        {
            if (sm->e[j].hits > sm->e[best].hits)
                best = j;
        }
        src_entry_t tmp = sm->e[t];
        sm->e[t]    = sm->e[best];
        sm->e[best] = tmp;
        snprintf(res->top_source[t], MAX_SOURCE_LEN, "%s", sm->e[t].name);
        res->top_source_hits[t] = sm->e[t].hits;
    }

    /* --- Mark this level as ready and notify the Aggregator. */
    res->ready = 1;
    pthread_cond_broadcast(&g_region_c->result_cond);
    pthread_mutex_unlock(&g_region_c->result_mutex);

    /* The semaphore acts as a redundant signal that some Aggregator implementations might prefer over cond_wait. */
    sem_post(&g_region_c->level_sems[g_level_idx]);

    printf("[PID:%d][TID:%d] Total entries: %ld | Weighted score: %.1f\n",getpid(), (int)my_tid, g_total_entries, g_total_weighted);
    fflush(stdout);

    return NULL;
}

/* analyzer_process_main - entry point of the Analyzer child process*/
void analyzer_process_main(analyzer_arg_t* a)
{
    /* Capture argument-derived state into the file-scope globals so that worker threads can see them. */
    g_level_idx       = a->level_idx;
    g_n_keywords      = a->n_keywords;
    g_n_workers       = a->n_workers;
    g_keywords        = a->keywords;
    g_region_b        = a->region_b;
    g_region_c        = a->region_c;
    g_tid_count       = 0;
    g_total_entries   = 0;
    g_total_weighted  = 0.0;
    g_destr_done      = 0;
    memset(&g_src_map, 0, sizeof(g_src_map));
    memset(g_tids,     0, sizeof(g_tids));

    pthread_mutex_init(&g_agg_mu,   NULL);
    pthread_mutex_init(&g_tid_mu,   NULL);
    pthread_mutex_init(&g_destr_mu, NULL);
    pthread_cond_init (&g_destr_cv, NULL);

    printf("[PID:%d] Analyzer %s started. Workers: %d\n", getpid(), LEVEL_NAMES[a->level_idx], a->n_workers);
    fflush(stdout);

    /* Create the TLS key with the destructor that flushes per-thread
     * scores to Region C. */
    pthread_key_create(&g_tls_key, tls_destructor);

    /* The barrier height is exactly the number of workers. */
    pthread_barrier_init(&g_barrier, NULL, (unsigned)a->n_workers);

    /* Spawn the worker threads. */
    pthread_t*    tids  = calloc(a->n_workers, sizeof(pthread_t));
    worker_arg_t* wargs = calloc(a->n_workers, sizeof(worker_arg_t));

    for (int w = 0; w < a->n_workers; w++)
    {
        wargs[w].worker_idx = w;
        if (pthread_create(&tids[w], NULL, worker_thread_func, &wargs[w]) != 0)
        {
            perror("pthread_create worker");
            exit(EXIT_FAILURE);
        }
    }

    /* Wait for every worker to finish. */
    for (int w = 0; w < a->n_workers; w++)
        pthread_join(tids[w], NULL);

    /* Tear down sync primitives. */
    pthread_barrier_destroy(&g_barrier);
    pthread_key_delete     (g_tls_key);
    pthread_mutex_destroy  (&g_agg_mu);
    pthread_mutex_destroy  (&g_tid_mu);
    pthread_mutex_destroy  (&g_destr_mu);
    pthread_cond_destroy   (&g_destr_cv);
    free(tids);
    free(wargs);

    printf("[PID:%d] Analyzer %s exiting.\n", getpid(), LEVEL_NAMES[a->level_idx]);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}