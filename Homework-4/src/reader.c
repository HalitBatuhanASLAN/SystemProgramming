#include "reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>

/* ─── Parse yardımcıları ────────────────────────────────────────────────── */
int level_from_string(const char* s) {
    if (strcmp(s, "ERROR") == 0) return LVL_ERROR;
    if (strcmp(s, "WARN")  == 0) return LVL_WARN;
    if (strcmp(s, "INFO")  == 0) return LVL_INFO;
    if (strcmp(s, "DEBUG") == 0) return LVL_DEBUG;
    return -1;
}

/* Döner: 1=başarı, 0=atla (malformed/blank) */
int parse_log_line(const char* line, log_entry_t* out) {
    memset(out, 0, sizeof(*out));

    /* [YYYY-MM-DD HH:MM:SS] */
    const char* p = line;
    if (*p != '[') return 0;
    p++;
    char ts[20]; int i = 0;
    while (*p && *p != ']' && i < 19) ts[i++] = *p++;
    ts[i] = '\0';
    if (*p != ']') return 0;
    p++; /* ] */
    if (*p != ' ') return 0;
    p++; /* ' ' */

    /* [LEVEL] */
    if (*p != '[') return 0;
    p++;
    char level_str[16]; i = 0;
    while (*p && *p != ']' && i < 15) level_str[i++] = *p++;
    level_str[i] = '\0';
    if (*p != ']') return 0;
    p++;
    if (*p != ' ') return 0;
    p++;

    int lvl = level_from_string(level_str);
    if (lvl < 0) return 0; /* bilinmeyen seviye → atla */

    /* [SOURCE] */
    if (*p != '[') return 0;
    p++;
    char src[MAX_SOURCE_LEN]; i = 0;
    while (*p && *p != ']' && i < MAX_SOURCE_LEN-1) src[i++] = *p++;
    src[i] = '\0';
    if (*p != ']') return 0;
    p++;

    /* Başındaki boşlukları sıyır */
    while (*p == ' ') p++;

    /* Sonucu doldur */
    memcpy(out->timestamp, ts, i);
    out->timestamp[i] = '\0';
    out->level = lvl;
    snprintf(out->source,  MAX_SOURCE_LEN, "%s", src);
    snprintf(out->message, MAX_MSG_LEN,    "%s", p);

    /* Sondaki \n kaldır */
    size_t mlen = strlen(out->message);
    if (mlen > 0 && out->message[mlen-1] == '\n')
        out->message[mlen-1] = '\0';

    out->is_eof = 0;
    return 1;
}

/* ─── İç buffer ─────────────────────────────────────────────────────────── */
static void ibuf_push(internal_buf_t* b, const log_entry_t* e) {
    pthread_mutex_lock(&b->mu);
    while (b->count == INTERNAL_BUF_CAP)
        pthread_cond_wait(&b->not_full, &b->mu);
    b->buf[b->tail] = *e;
    b->tail = (b->tail + 1) % INTERNAL_BUF_CAP;
    b->count++;
    pthread_cond_signal(&b->not_empty);
    pthread_mutex_unlock(&b->mu);
}

static int ibuf_pop(internal_buf_t* b, log_entry_t* e) {
    pthread_mutex_lock(&b->mu);
    while (b->count == 0) {
        if (b->finished_producers >= b->n_producers) {
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

/* ─── Reader Thread ─────────────────────────────────────────────────────── */
void* reader_thread_func(void* arg) {
    reader_thread_arg_t* a = (reader_thread_arg_t*)arg;

    FILE* f = fopen(a->filepath, "r");
    if (!f) { perror("fopen reader_thread"); goto done; }

    /* Başlangıç pozisyonunu ayarla */
    if (a->start_byte > 0) {
        fseek(f, a->start_byte, SEEK_SET);
        /* İlk satırı atla (önceki thread tamamlar) */
        char skip[4096];
        if (!fgets(skip, sizeof(skip), f)) { fclose(f); goto done; }
    }

    printf("[PID:%d][TID:reader%d] Reader thread %d: range [%ld, %ld)\n",
           getpid(), a->thread_idx, a->thread_idx, a->start_byte,
           a->end_byte < 0 ? -1L : a->end_byte);
    fflush(stdout);

    long lines_read = 0, malformed = 0;
    char line[MAX_MSG_LEN + 256];

    while (fgets(line, sizeof(line), f)) {
        long cur_pos = ftell(f);
        /* Bitiş kontrolü */
        if (a->end_byte >= 0 && cur_pos > a->end_byte) break;

        /* Boş satır */
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;

        log_entry_t entry;
        if (parse_log_line(line, &entry)) {
            ibuf_push(a->ibuf, &entry);
            lines_read++;
        } else {
            malformed++;
        }

        /* Heartbeat: her 50 satırda bir */
        if (lines_read > 0 && lines_read % 50 == 0) {
            char hb[128];
            int hlen = snprintf(hb, sizeof(hb),
                "[R%d] %ld lines processed\n", a->reader_idx, lines_read);
            if (write(a->pipe_write_fd, hb, hlen) < 0) { /* heartbeat best-effort */ }
        }
    }

    /* Son heartbeat */
    char hb[128];
    int hlen = snprintf(hb, sizeof(hb),
        "[R%d] %ld lines processed\n", a->reader_idx, lines_read);
    if (write(a->pipe_write_fd, hb, hlen) < 0) { /* best-effort */ }

    printf("[PID:%d][TID:reader%d] Reader thread %d: finished, lines_read=%ld, malformed=%ld\n",
           getpid(), a->thread_idx, a->thread_idx, lines_read, malformed);
    fflush(stdout);

    fclose(f);

done:
    pthread_mutex_lock(&a->ibuf->mu);
    a->ibuf->finished_producers++;
    pthread_cond_broadcast(&a->ibuf->not_empty);
    pthread_mutex_unlock(&a->ibuf->mu);
    return NULL;
}

/* ─── Parser Thread ─────────────────────────────────────────────────────── */
void* parser_thread_func(void* arg) {
    parser_thread_arg_t* a = (parser_thread_arg_t*)arg;
    long counts[LEVEL_COUNT] = {0};

    log_entry_t entry;
    while (ibuf_pop(a->ibuf, &entry)) {
        counts[entry.level]++;
        shm_a_push(a->region_a, &entry);
    }

    /* EOF marker'ları gönder */
    for (int lvl = 0; lvl < LEVEL_COUNT; lvl++) {
        log_entry_t eof;
        memset(&eof, 0, sizeof(eof));
        eof.is_eof = 1;
        eof.level  = lvl;
        shm_a_push(a->region_a, &eof);
    }

    printf("[PID:%d] Parser thread: dispatched E:%ld W:%ld I:%ld D:%ld -> Region A\n",
           getpid(), counts[0], counts[1], counts[2], counts[3]);
    fflush(stdout);
    return NULL;
}

/* ─── Reader Process Main ───────────────────────────────────────────────── */
void reader_process_main(reader_proc_arg_t* a) {
    printf("[PID:%d] Reader %d started. File: %s, Threads: %d\n",
           getpid(), a->reader_idx, a->filepath, a->n_threads);
    fflush(stdout);

    /* Dosya boyutunu bul */
    struct stat st;
    if (stat(a->filepath, &st) < 0) {
        perror("stat"); exit(EXIT_FAILURE);
    }
    long file_size = st.st_size;

    /* İç buffer başlat */
    internal_buf_t ibuf;
    memset(&ibuf, 0, sizeof(ibuf));
    pthread_mutex_init(&ibuf.mu, NULL);
    pthread_cond_init(&ibuf.not_full, NULL);
    pthread_cond_init(&ibuf.not_empty, NULL);
    ibuf.n_producers = a->n_threads;
    ibuf.finished_producers = 0;

    /* Reader thread argümanlarını hazırla */
    reader_thread_arg_t* rargs = calloc(a->n_threads, sizeof(*rargs));
    pthread_t*           rtids = calloc(a->n_threads, sizeof(pthread_t));

    long chunk = file_size / a->n_threads;
    for (int t = 0; t < a->n_threads; t++) {
        rargs[t].thread_idx   = t;
        rargs[t].filepath     = a->filepath;
        rargs[t].start_byte   = t * chunk;
        rargs[t].end_byte     = (t == a->n_threads-1) ? -1 : (t+1)*chunk - 1;
        rargs[t].reader_idx   = a->reader_idx;
        rargs[t].pipe_write_fd= a->pipe_write_fd;
        rargs[t].ibuf         = &ibuf;

        printf("[PID:%d][TID:reader%d] Reader thread %d: range [%ld, %ld) bytes\n",
               getpid(), t, t, rargs[t].start_byte,
               rargs[t].end_byte < 0 ? file_size : rargs[t].end_byte);
        fflush(stdout);

        if (pthread_create(&rtids[t], NULL, reader_thread_func, &rargs[t]) != 0) {
            perror("pthread_create reader_thread"); exit(EXIT_FAILURE);
        }
    }

    /* Parser thread */
    parser_thread_arg_t parg = {&ibuf, a->region_a, a->reader_idx};
    pthread_t ptid;
    if (pthread_create(&ptid, NULL, parser_thread_func, &parg) != 0) {
        perror("pthread_create parser_thread"); exit(EXIT_FAILURE);
    }

    /* Tüm thread'leri bekle */
    for (int t = 0; t < a->n_threads; t++)
        pthread_join(rtids[t], NULL);

    /* Parser thread'e sinyal: tüm üreticiler bitti */
    pthread_mutex_lock(&ibuf.mu);
    pthread_cond_broadcast(&ibuf.not_empty);
    pthread_mutex_unlock(&ibuf.mu);

    pthread_join(ptid, NULL);

    /* Temizlik */
    pthread_mutex_destroy(&ibuf.mu);
    pthread_cond_destroy(&ibuf.not_full);
    pthread_cond_destroy(&ibuf.not_empty);
    free(rargs);
    free(rtids);

    printf("[PID:%d] Reader %d exiting.\n", getpid(), a->reader_idx);
    fflush(stdout);
    exit(EXIT_SUCCESS);
}
