/*
 * reader.c - Reader process implementation
 *
 * Responsibilities:
 *   1. Fork-time entry point reader_process_main() splits the log file into T contiguous byte-range chunks and spawns T reader threads.
 *   2. Each reader thread reads its chunk, parses every well-formed line into a log_entry_t, and pushes it into the in-process internal buffer (ibuf_push). It also writes a heartbeat to a pipe so the Watchdog can show progress.
 *   3. A separate parser thread drains the internal buffer (ibuf_pop) and forwards each entry to Region A (cross-process buffer). When the internal buffer is permanently empty (all reader threads finished AND the buffer is drained), the parser thread pushes one EOF marker per log level into Region A and exits.
 *
 * Chunk boundary handling
 * -----------------------
 * Splitting a file by raw byte offsets can land in the middle of a line.
 * Convention: a thread that does NOT start at offset 0 first calls fgets() once and discards the (possibly partial) line. The previous chunk's thread will pick up that line because fgets reads until a newline, which may carry the read pointer past end_byte. This guarantees every complete line is read by exactly one thread. */

#include "reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>

/*
 * level_from_string - "ERROR" / "WARN" / "INFO" / "DEBUG" -> integer index
 * Anything else returns -1, telling the caller to skip the line.
*/
int level_from_string(const char* s)
{
    if (strcmp(s, "ERROR") == 0) return LVL_ERROR;
    if (strcmp(s, "WARN")  == 0) return LVL_WARN;
    if (strcmp(s, "INFO")  == 0) return LVL_INFO;
    if (strcmp(s, "DEBUG") == 0) return LVL_DEBUG;
    return -1;
}

/*
 * parse_log_line - parse a single line into log_entry_t
 * Expected format:
 *     [YYYY-MM-DD HH:MM:SS] [LEVEL] [SOURCE] free-form message
 *
 * Returns 1 on success, 0 on any deviation (malformed line). In the 0 case the caller treats the line as malformed and skips it.
 *
 * The function is hand-written rather than using sscanf so that long messages with embedded brackets are handled correctly.
*/
int parse_log_line(const char* line, log_entry_t* out)
{
    memset(out, 0, sizeof(*out));

    const char* p = line;

    /* --- [YYYY-MM-DD HH:MM:SS] --- */
    if (*p != '[') return 0;
    p++;
    char ts[20];
    int  i = 0;
    while (*p && *p != ']' && i < 19)
        ts[i++] = *p++;

    ts[i] = '\0';
    if (*p != ']') return 0;
    p++;
    if (*p != ' ') return 0;
    p++;

    /* --- [LEVEL] --- */
    if (*p != '[') return 0;
    p++;
    char level_str[16];
    i = 0;
    while (*p && *p != ']' && i < 15)
        level_str[i++] = *p++;

    level_str[i] = '\0';
    if (*p != ']') return 0;
    p++;
    if (*p != ' ') return 0;
    p++;

    int lvl = level_from_string(level_str);
    if (lvl < 0) return 0;   /* unknown LEVEL -> silently skip the line */

    /* --- [SOURCE] --- */
    if (*p != '[') return 0;
    p++;
    char src[MAX_SOURCE_LEN];
    i = 0;
    while (*p && *p != ']' && i < MAX_SOURCE_LEN - 1)
        src[i++] = *p++;

    src[i] = '\0';
    if (*p != ']') return 0;
    p++;

    /* Strip leading whitespace before the message. */
    while (*p == ' ' || *p == '\t')
        p++;

    /* Fill the output struct. */
    snprintf(out->timestamp, MAX_TS_LEN, "%s", ts);
    out->level = lvl;
    snprintf(out->source,  MAX_SOURCE_LEN, "%s", src);
    snprintf(out->message, MAX_MSG_LEN,    "%s", p);

    /* Trim trailing CR/LF (handles both Unix \n and Windows \r\n). */
    size_t mlen = strlen(out->message);
    while (mlen > 0 && (out->message[mlen - 1] == '\n' || out->message[mlen - 1] == '\r'))
        out->message[--mlen] = '\0';

    out->is_eof = 0;
    return 1;
}

/*
 * Internal buffer push/pop (reader threads -> parser thread, in-process)
*/

/* ibuf_push: classic bounded-buffer push. Block on not_full if full. */
static void ibuf_push(internal_buf_t* b, const log_entry_t* e)
{
    pthread_mutex_lock(&b->mu);
    while(b->count == INTERNAL_BUF_CAP)
        pthread_cond_wait(&b->not_full, &b->mu);

    b->buf[b->tail] = *e;
    b->tail = (b->tail + 1) % INTERNAL_BUF_CAP;
    b->count++;
    pthread_cond_signal(&b->not_empty);
    pthread_mutex_unlock(&b->mu);
}

/* ibuf_pop: returns 0 only when buffer is empty AND every reader thread has reported finished_producers++. */
static int ibuf_pop(internal_buf_t* b, log_entry_t* e)
{
    pthread_mutex_lock(&b->mu);
    while(b->count == 0)
    {
        if(b->finished_producers >= b->n_producers)
        {
            pthread_mutex_unlock(&b->mu);
            return 0;
        }
        pthread_cond_wait(&b->not_empty, &b->mu);
    }
    *e = b->buf[b->head];
    b->head = (b->head + 1) % INTERNAL_BUF_CAP;
    b->count--;
    pthread_cond_signal(&b->not_full);
    pthread_mutex_unlock(&b->mu);
    return 1;
}

/*
 * reader_thread_func - one of T threads inside a Reader process
 * Reads its byte-range chunk, parses lines, pushes them into ibuf, and sends heartbeat strings down the pipe to the Watchdog. */
void* reader_thread_func(void* arg)
{
    reader_thread_arg_t* a = (reader_thread_arg_t*)arg;

    FILE* f = fopen(a->filepath, "r");
    if (!f)
    {
        perror("fopen reader_thread");
        goto done;
    }

    /* Seek to start_byte. If we are not at the very beginning, the preceding chunk is responsible for finishing the partial line we are sitting on, so we read and discard one line. */
    if (a->start_byte > 0)
    {
        if(fseek(f, a->start_byte, SEEK_SET) != 0)
        {
            perror("fseek");
            fclose(f);
            goto done;
        }
        char skip[4096];
        if(!fgets(skip, sizeof(skip), f))
        {
            fclose(f);
            goto done;
        }
    }

    long lines_read = 0;
    long malformed  = 0;
    char line[MAX_MSG_LEN + 256];

    while(fgets(line, sizeof(line), f))
    {
        long cur_pos = ftell(f);

        /* Empty / whitespace-only line: skip silently. */
        if(line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        {
            if (a->end_byte >= 0 && cur_pos > a->end_byte)
                break;
            continue;
        }

        log_entry_t entry;
        if (parse_log_line(line, &entry))
        {
            ibuf_push(a->ibuf, &entry);
            lines_read++;
        }
        else
            malformed++;

        /* Heartbeat: every 50 lines write a small message to the pipe
         * that the Watchdog reads. Best-effort, no error checking. */
        if(lines_read > 0 && lines_read % 50 == 0)
        {
            char hb[128];
            int  hlen = snprintf(hb, sizeof(hb), "[R%d] %ld lines processed\n", a->reader_idx, lines_read);
            ssize_t wn = write(a->pipe_write_fd, hb, (size_t)hlen);
            (void)wn;
        }

        /* Stop once we have crossed past end_byte. fgets always finishes
         * the current line, so we may overshoot end_byte by a few bytes
         * which is exactly the desired behavior. */
        if (a->end_byte >= 0 && cur_pos > a->end_byte)
            break;
    }

    /* Final heartbeat before exit (in case lines_read is not a multiple of 50). */
    {
        char hb[128];
        int  hlen = snprintf(hb, sizeof(hb), "[R%d] %ld lines processed\n", a->reader_idx, lines_read);
        ssize_t wn = write(a->pipe_write_fd, hb, (size_t)hlen);
        (void)wn;
    }

    printf("[PID:%d][TID:reader%d] Reader thread %d: finished, "
           "lines_read=%ld, malformed=%ld\n",
           getpid(), a->thread_idx, a->thread_idx, lines_read, malformed);
    fflush(stdout);

    fclose(f);

done:
    /* Mark this producer as finished, then wake the parser thread in case it was sleeping on not_empty. */
    pthread_mutex_lock(&a->ibuf->mu);
    a->ibuf->finished_producers++;
    pthread_cond_broadcast(&a->ibuf->not_empty);
    pthread_mutex_unlock(&a->ibuf->mu);
    return NULL;
}

/* parser_thread_func - drains internal buffer into Region A
 * After all reader threads have finished and the buffer is empty, pushes one EOF marker per log level so consumers know "this Reader is done". */
void* parser_thread_func(void* arg)
{
    parser_thread_arg_t* a = (parser_thread_arg_t*)arg;
    long counts[LEVEL_COUNT] = {0};

    log_entry_t entry;
    while(ibuf_pop(a->ibuf, &entry))
    {
        if(entry.level >= 0 && entry.level < LEVEL_COUNT)
            counts[entry.level]++;
        shm_a_push(a->region_a, &entry);
    }

    /* Push one EOF marker per level. Region A counts these so the
     * Dispatcher can detect when EVERY Reader has finished EVERY level. */
    for(int lvl = 0; lvl < LEVEL_COUNT; lvl++)
    {
        log_entry_t eof;
        memset(&eof, 0, sizeof(eof));
        eof.is_eof = 1;
        eof.level  = lvl;
        shm_a_push(a->region_a, &eof);
    }

    printf("[PID:%d] Parser thread: dispatched E:%ld W:%ld I:%ld D:%ld "
           "-> Region A\n",
           getpid(), counts[0], counts[1], counts[2], counts[3]);
    fflush(stdout);
    return NULL;
}

/*
 * reader_process_main - entry point of the Reader child process
*/
void reader_process_main(reader_proc_arg_t* a)
{
    printf("[PID:%d] Reader %d started. File: %s, Threads: %d\n", getpid(), a->reader_idx, a->filepath, a->n_threads);
    fflush(stdout);

    /* Determine total file size for chunk math. */
    struct stat st;
    if (stat(a->filepath, &st) < 0)
    {
        perror("stat");
        exit(EXIT_FAILURE);
    }
    long file_size = st.st_size;

    /* Initialize the in-process buffer (mutex + 2 cond vars). */
    internal_buf_t ibuf;
    memset(&ibuf, 0, sizeof(ibuf));
    pthread_mutex_init(&ibuf.mu, NULL);
    pthread_cond_init (&ibuf.not_full, NULL);
    pthread_cond_init (&ibuf.not_empty, NULL);
    ibuf.n_producers = a->n_threads;
    ibuf.finished_producers = 0;

    /* Allocate per-thread argument arrays. */
    reader_thread_arg_t* rargs = calloc(a->n_threads, sizeof(*rargs));
    pthread_t* rtids = calloc(a->n_threads, sizeof(pthread_t));
    if (!rargs || !rtids)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    /* Compute even chunk size. If the file is smaller than n_threads, the first thread takes the whole file and the others see an empty range and exit immediately. */
    int effective_threads = a->n_threads;
    long chunk = (effective_threads > 0) ? file_size / effective_threads : 0;
    if (chunk == 0 && effective_threads > 1)
        chunk = file_size;

    /* Spawn reader threads. */
    for (int t = 0; t < a->n_threads; t++)
    {
        rargs[t].thread_idx = t;
        rargs[t].filepath = a->filepath;
        rargs[t].start_byte = (long)t * chunk;
        rargs[t].end_byte = (t == a->n_threads - 1) ? -1 : (long)(t + 1) * chunk - 1;
        rargs[t].reader_idx = a->reader_idx;
        rargs[t].pipe_write_fd = a->pipe_write_fd;
        rargs[t].ibuf = &ibuf;

        printf("[PID:%d][TID:reader%d] Reader thread %d: range [%ld, %ld) bytes\n", getpid(), t, t, rargs[t].start_byte, rargs[t].end_byte < 0 ? file_size : rargs[t].end_byte);
        fflush(stdout);

        if (pthread_create(&rtids[t], NULL, reader_thread_func, &rargs[t]) != 0)
        {
            perror("pthread_create reader_thread");
            exit(EXIT_FAILURE);
        }
    }

    /* Spawn the single parser thread that drains ibuf into Region A. */
    parser_thread_arg_t parg = { &ibuf, a->region_a, a->reader_idx };
    pthread_t  ptid;
    if (pthread_create(&ptid, NULL, parser_thread_func, &parg) != 0)
    {
        perror("pthread_create parser_thread");
        exit(EXIT_FAILURE);
    }

    /* Join all reader threads first; this guarantees finished_producers
     * has reached n_producers when the parser thread checks it next. */
    for (int t = 0; t < a->n_threads; t++)
        pthread_join(rtids[t], NULL);

    /* Kick the parser one more time in case it is asleep on not_empty. */
    pthread_mutex_lock(&ibuf.mu);
    pthread_cond_broadcast(&ibuf.not_empty);
    pthread_mutex_unlock(&ibuf.mu);

    pthread_join(ptid, NULL);

    /* Clean up local sync primitives. */
    pthread_mutex_destroy(&ibuf.mu);
    pthread_cond_destroy (&ibuf.not_full);
    pthread_cond_destroy (&ibuf.not_empty);
    free(rargs);
    free(rtids);

    printf("[PID:%d] Reader %d exiting.\n", getpid(), a->reader_idx);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}