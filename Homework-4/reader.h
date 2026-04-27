#ifndef READER_H
#define READER_H

/* =============================================================================
 * reader.h - Reader process and thread interfaces
 * =============================================================================
 *
 * Each Reader process is forked by main, given one log file path and a
 * thread count T. Inside the Reader process there are:
 *
 *   - T "reader threads" that each read a byte-range chunk of the log
 *     file, parse the lines they see, and push the parsed entries into
 *     an internal_buf_t (a small in-process ring buffer).
 *
 *   - 1 "parser thread" that drains the internal buffer and forwards
 *     every entry into Region A (the cross-process shared buffer).
 *     When the internal buffer is exhausted and all reader threads are
 *     done, the parser thread pushes one EOF marker per log level into
 *     Region A, then exits.
 *
 * The split into reader threads + parser thread isolates I/O parallelism
 * from the cross-process synchronization cost.
 *
 * The internal_buf_t uses ONLY pthread_mutex + pthread_cond - no
 * semaphores, in line with the assignment's restriction.
 * ============================================================================= */

#include "shm.h"

/* Capacity of the in-process internal buffer (reader -> parser).           */
#define INTERNAL_BUF_CAP 256

/* =============================================================================
 * internal_buf_t - in-process ring buffer used between reader threads
 * (producers) and the parser thread (consumer). Lives on the heap of the
 * Reader process; NOT visible to other processes.
 * ============================================================================= */
typedef struct
{
    pthread_mutex_t mu;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
    log_entry_t     buf[INTERNAL_BUF_CAP];
    int             head, tail, count;
    int             n_producers;        /* expected number of reader threads*/
    int             finished_producers; /* incremented by reader threads    */
} internal_buf_t;

/* =============================================================================
 * reader_proc_arg_t - parameters passed to reader_process_main()
 * ============================================================================= */
typedef struct
{
    int              reader_idx;        /* 0..n_files-1                     */
    const char*      filepath;          /* path of the log file to read     */
    int              n_threads;         /* T - reader thread count          */
    int              pipe_write_fd;     /* heartbeat pipe to Watchdog       */
    shm_region_a_t*  region_a;          /* shared input buffer              */
    int              n_readers_total;   /* n_files (informational)          */
} reader_proc_arg_t;

/* =============================================================================
 * reader_thread_arg_t - parameters for each reader thread inside a Reader.
 * ============================================================================= */
typedef struct
{
    int             thread_idx;         /* 0..T-1 inside this Reader        */
    const char*     filepath;
    long            start_byte;         /* inclusive start of the chunk     */
    long            end_byte;           /* inclusive end of chunk; -1=EOF   */
    int             reader_idx;         /* same as Reader.reader_idx        */
    int             pipe_write_fd;
    internal_buf_t* ibuf;
} reader_thread_arg_t;

/* =============================================================================
 * parser_thread_arg_t - parameters for the parser thread.
 * ============================================================================= */
typedef struct
{
    internal_buf_t* ibuf;
    shm_region_a_t* region_a;
    int             reader_idx;
} parser_thread_arg_t;

/* Entry point: called by the child after fork. Never returns (exits).      */
void reader_process_main(reader_proc_arg_t* arg);

/* Parses a single log line into log_entry_t. Returns 1 on success,
 * 0 if the line is malformed and should be skipped silently.               */
int parse_log_line(const char* line, log_entry_t* out);

/* Maps "ERROR" / "WARN" / "INFO" / "DEBUG" to the LVL_* index.
 * Returns -1 for any other string.                                         */
int level_from_string(const char* s);

#endif /* READER_H */