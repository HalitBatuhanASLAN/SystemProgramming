#ifndef SHM_H
#define SHM_H

/* shm.h - Shared Memory Layout and Synchronization Primitives
 
 * This header defines the four shared memory regions (A, B, C, D) used to
 * communicate between the cooperating processes:
 *
 *   Region A : single ring buffer that holds parsed log entries from all
 *              Readers, consumed by the Dispatcher.
 *   Region B : an ARRAY of four ring buffers, one per log level
 *              (ERROR, WARN, INFO, DEBUG). Filled by the Dispatcher,
 *              drained by the matching Analyzer process.
 *   Region C : a results area where each Analyzer publishes its final
 *              level_result_t, consumed by the Aggregator.
 *   Region D : a high-priority "shadow" ring buffer. The Dispatcher
 *              copies entries here when the entry's source matches the
 *              priority filter file. Drained by a thread inside the
 *              Aggregator.
 *
 * All four regions live in anonymous shared memory (mmap + MAP_SHARED)
 * because every process has to see exactly the same bytes; they are also
 * created BEFORE the first fork() so child processes inherit the mapping.
 *
 * Synchronization primitives stored inside these regions (mutexes, cond
 * vars, semaphores) are initialized with the PTHREAD_PROCESS_SHARED
 * attribute so they work across process boundaries (see shm.c).*/

#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>

/* Maximum number of comma-separated keywords accepted on the command line. */
#define MAX_KEYWORDS 8

/* Maximum number of worker threads (-w) per Analyzer process.*/
#define MAX_WORKERS 64

/* Maximum length of a "source" tag inside [SOURCE] brackets.*/
#define MAX_SOURCE_LEN 64

/* Maximum length of the message portion of a log line.*/
#define MAX_MSG_LEN 1024

/* Maximum length of the timestamp string ("YYYY-MM-DD HH:MM:SS\0").*/
#define MAX_TS_LEN 20

/* Number of distinct log levels (ERROR, WARN, INFO, DEBUG).*/
#define LEVEL_COUNT 4

/* Log level indices used everywhere as array indices.*/
#define LVL_ERROR  0
#define LVL_WARN   1
#define LVL_INFO   2
#define LVL_DEBUG  3

/* String name for each level, used in printf and in the .txt output.*/
extern const char* LEVEL_NAMES[LEVEL_COUNT];

/* Multiplicative weight applied to each level when scoring keyword hits.
 * Per the assignment: ERROR=4, WARN=3, INFO=2, DEBUG=1.*/
extern const int   LEVEL_WEIGHTS[LEVEL_COUNT];

/*
 * log_entry_t - The smallest unit of data flowing through the pipeline.
 * Also doubles as an EOF sentinel when is_eof = 1.*/
typedef struct
{
    char timestamp[MAX_TS_LEN];      /* "YYYY-MM-DD HH:MM:SS"                */
    int  level;                      /* LVL_ERROR..LVL_DEBUG                 */
    char source[MAX_SOURCE_LEN];     /* contents of [SOURCE] brackets        */
    char message[MAX_MSG_LEN];       /* free-form message after [SOURCE]     */
    int  is_eof;                     /* 1 = EOF sentinel, 0 = real entry     */
} log_entry_t;

/*
 * level_result_t - Final result for a single log level.
 * Filled by the matching Analyzer's reporting thread, read by the Aggregator.*/
typedef struct
{
    char   level[8];                          /* "ERROR" / "WARN" / ...      */
    long   total_entries;                     /* count of entries this level */
    double total_weighted_score;              /* sum of weighted hits        */
    double per_keyword_score[MAX_KEYWORDS];   /* breakdown by keyword        */
    double per_thread_score[MAX_WORKERS];     /* breakdown by worker thread  */
    char   top_source[3][MAX_SOURCE_LEN];     /* top-3 source names          */
    long   top_source_hits[3];                /* their hit counts            */
    int    ready;                             /* set by Analyzer when done   */
} level_result_t;

/* Region A - input ring buffer (Reader -> Dispatcher).
 * eof_count_per_level[i] reaches total_readers once every Reader has pushed an EOF marker for level i.*/
typedef struct
{
    pthread_mutex_t input_mutex;
    pthread_cond_t  not_full_a;
    pthread_cond_t  not_empty_a;

    int eof_count_per_level[LEVEL_COUNT];
    int total_readers;

    int head, tail, count, capacity;
    log_entry_t buf[];                    /* C99 flexible array */
} shm_region_a_t;

/* 
 * Region B - per-level ring buffer (Dispatcher -> Analyzer).
 * One instance per level (so four total). */
typedef struct
{
    pthread_mutex_t level_mutex;
    pthread_cond_t  not_full_b;
    pthread_cond_t  not_empty_b;

    int eof_posted;/* 1 = no more entries coming  */

    int head, tail, count, capacity;
    log_entry_t buf[];
} shm_region_b_t;

/* Region C - results region (Analyzer -> Aggregator).
 * The Aggregator waits on result_cond + sem_timedwait for each level.*/
typedef struct
{
    pthread_mutex_t result_mutex;
    pthread_cond_t result_cond;
    sem_t level_sems[LEVEL_COUNT];
    level_result_t results[LEVEL_COUNT];
} shm_region_c_t;

/* Region D - high-priority shadow buffer (Dispatcher -> Aggregator).
 * The Dispatcher pushes entries whose source is in the priority filter list; a thread inside the Aggregator drains them.
*/
typedef struct
{
    pthread_mutex_t priority_mutex;
    pthread_cond_t  not_full_d;
    pthread_cond_t  not_empty_d;

    int dispatcher_done;/* set when Dispatcher exits*/

    int head, tail, count, capacity;
    log_entry_t buf[];
} shm_region_d_t;

/* Size helpers (needed because the structs end with a flexible array).*/
size_t shm_region_a_size(int capacity);
size_t shm_region_b_size(int capacity);
size_t shm_region_d_size(int capacity);

/* Region creation (called BEFORE fork by main).*/
shm_region_a_t* shm_create_region_a(int capacity);
shm_region_b_t* shm_create_region_b(int capacity);
shm_region_c_t* shm_create_region_c(void);
shm_region_d_t* shm_create_region_d(int capacity);

/* Region destruction (called AFTER waitpid by main).*/
void shm_destroy_region_a(shm_region_a_t* r);
void shm_destroy_region_b(shm_region_b_t* r, int capacity);
void shm_destroy_region_c(shm_region_c_t* r);
void shm_destroy_region_d(shm_region_d_t* r, int capacity);

/* Region A operations.*/
void shm_a_push(shm_region_a_t* r, const log_entry_t* e);
int shm_a_pop_timed(shm_region_a_t* r, log_entry_t* e, int timeout_sec);

/* Region B operations.*/
void shm_b_push(shm_region_b_t* r, const log_entry_t* e);
int shm_b_pop(shm_region_b_t* r, log_entry_t* e);

/* Region D operations.*/
void shm_d_push(shm_region_d_t* r, const log_entry_t* e);
int shm_d_pop(shm_region_d_t* r, log_entry_t* e);

#endif