#ifndef READER_H
#define READER_H

#include "shm.h"

#define INTERNAL_BUF_CAP 256

/* İç dairesel buffer (Reader sürecine özel, process-shared değil) */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
    log_entry_t     buf[INTERNAL_BUF_CAP];
    int             head, tail, count;
    int             producers_done; /* tüm reader thread'ler bitince 1 */
    int             n_producers;
    int             finished_producers;
} internal_buf_t;

typedef struct {
    int              reader_idx;
    const char*      filepath;
    int              n_threads;        /* T */
    int              pipe_write_fd;
    shm_region_a_t*  region_a;
    int              n_readers_total;  /* Region A'ya yazılacak total_readers */
} reader_proc_arg_t;

typedef struct {
    int             thread_idx;
    const char*     filepath;
    long            start_byte;
    long            end_byte;        /* -1: dosya sonu */
    int             reader_idx;
    int             pipe_write_fd;
    internal_buf_t* ibuf;
} reader_thread_arg_t;

typedef struct {
    internal_buf_t* ibuf;
    shm_region_a_t* region_a;
    int             reader_idx;
} parser_thread_arg_t;

/* Giriş noktası: fork sonrası çocuk süreçte çağrılır */
void reader_process_main(reader_proc_arg_t* arg);

/* Log satırı parse */
int parse_log_line(const char* line, log_entry_t* out);
int level_from_string(const char* s);

#endif /* READER_H */
