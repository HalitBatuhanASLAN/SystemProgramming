/* 
 * main.c - Parent process: argument parsing, fork orchestration, signal
 *          handling, and final cleanup
 * Responsibilities of main:
 *   1. Parse command-line flags (-c, -f, -k, -t, -w, -a, -b, -d, -T, -o, -O).
 *   2. Read config file (list of log paths) and filter file (priority list).
 *   3. Allocate every shared region BEFORE forking - children inherit them.
 *   4. Set up SIGINT handler (sigaction, async-signal-safe flag only).
 *   5. fork() once per Reader, once for Dispatcher, four times for Analyzers, once for Aggregator, registering each child PID for later waitpid().
 *   6. Spawn the Watchdog thread.
 *   7. waitpid() loop until every child has exited, handling SIGINT cleanup.
 *   8. Print SYSTEM SUMMARY block, destroy shared regions, free buffers.*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>

#include "shm.h"
#include "reader.h"
#include "dispatcher.h"
#include "analyzer.h"
#include "aggregator.h"
#include "watchdog.h"

#define MAX_CHILDREN 256

/*
 * Globals
 * sigint_received and child_pids must be touched from the signal handler, so they live at file scope.
*/

/* Set to 1 by the SIGINT handler. The main loop polls this and triggers graceful shutdown when it becomes non-zero.*/
static volatile sig_atomic_t sigint_received = 0;

static pid_t child_pids[MAX_CHILDREN];
static int n_children = 0;

/* Shared memory pointers, used at exit time for cleanup. */
static shm_region_a_t* g_region_a = NULL;
static shm_region_b_t* g_region_b[LEVEL_COUNT];
static shm_region_c_t* g_region_c = NULL;
static shm_region_d_t* g_region_d = NULL;
static int g_cap_b = 0;
static int g_cap_d = 0;

/* "How many child processes have NOT yet been reaped". The Watchdog thread reads this; main writes it. Marked volatile so neither side caches a stale copy. */
static volatile int    g_children_alive = 0;

/*
 * sigint_handler - async-signal-safe SIGINT handler
 * Only sets a flag. No printf/malloc/etc here, because most libc functions are NOT safe to call from signal handlers. */
static void sigint_handler(int sig)
{
    (void)sig;
    sigint_received = 1;
}

/* register_child - record a child's PID for later waitpid(). */
static void register_child(pid_t pid)
{
    if (n_children < MAX_CHILDREN)
        child_pids[n_children++] = pid;
}

/*
 * cleanup_and_exit - SIGINT-path cleanup. Called from MAIN thread (not from the signal handler), so printf/free/etc are safe here.
*/
static void cleanup_and_exit(int code)
{
    shutdown_watchdog = 1;
    if (g_region_a)
        shm_destroy_region_a(g_region_a);
    for (int i = 0; i < LEVEL_COUNT; i++)
    {
        if (g_region_b[i])
            shm_destroy_region_b(g_region_b[i], g_cap_b);
    }
    if (g_region_c)
        shm_destroy_region_c(g_region_c);
    if (g_region_d)
        shm_destroy_region_d(g_region_d, g_cap_d);
    _exit(code);
}

/*
 * read_lines - read newline-separated entries into a malloc'ed string array
 * Used for the config file (log paths) and the priority filter file.
 * Out: *out_count is set to the line count; the array is heap-allocated.
*/
static char** read_lines(const char* path, int* out_count)
{
    FILE* f = fopen(path, "r");
    if (!f)
    {
        perror(path);
        exit(EXIT_FAILURE);
    }
    char** lines = NULL;
    int    count = 0;
    char   buf[512];
    while (fgets(buf, sizeof(buf), f))
    {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r'))
            buf[--l] = '\0';
        if (l == 0)
            continue;
        char** tmp = realloc(lines, (count + 1) * sizeof(char*));
        if (!tmp)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        lines = tmp;
        lines[count++] = strdup(buf);
    }
    fclose(f);
    *out_count = count;
    return lines;
}

/*
 * parse_keywords - split "a,b,c" into {"a","b","c"}, capped at MAX_KEYWORDS
*/
static char** parse_keywords(const char* kw_str, int* out_count)
{
    char*  dup   = strdup(kw_str);
    char** kws   = NULL;
    int    count = 0;
    char*  tok   = strtok(dup, ",");
    while (tok && count < MAX_KEYWORDS)
    {
        char** tmp = realloc(kws, (count + 1) * sizeof(char*));
        if (!tmp)
        {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        kws = tmp;
        kws[count++] = strdup(tok);
        tok = strtok(NULL, ",");
    }
    free(dup);
    *out_count = count;
    return kws;
}

/*
 * usage - print help text and exit
*/
static void usage(const char* prog)
{
    fprintf(stderr,
        "Kullanim: %s -c <config> -f <filter> -k <keywords>\n"
        "  -t <reader_threads> -w <worker_threads>\n"
        "  -a <cap_A> -b <cap_B> -d <cap_D>\n"
        "  -T <timeout> -o <output> -O <binary>\n"
        "Default -T: 10 sec\n", prog);
    exit(EXIT_FAILURE);
}

/* =============================================================================
 * main
 * ============================================================================= */
int main(int argc, char* argv[])
{
    /* --- Default argument values --- */
    const char* config_file = NULL;
    const char* filter_file = NULL;
    const char* keyword_str = NULL;
    int t_readers = 2;
    int w_workers = 2;
    int cap_a = 64;
    int cap_b = 32;
    int cap_d = 16;
    int timeout_sec = 10;
    const char* output_path = NULL;
    const char* binary_path = NULL;

    /* --- Parse CLI flags via getopt(3). --- */
    int opt;
    while ((opt = getopt(argc, argv, "c:f:k:t:w:a:b:d:T:o:O:")) != -1)
    {
        switch (opt)
        {
            case 'c': config_file = optarg;       break;
            case 'f': filter_file = optarg;       break;
            case 'k': keyword_str = optarg;       break;
            case 't': t_readers   = atoi(optarg); break;
            case 'w': w_workers   = atoi(optarg); break;
            case 'a': cap_a       = atoi(optarg); break;
            case 'b': cap_b       = atoi(optarg); break;
            case 'd': cap_d       = atoi(optarg); break;
            case 'T': timeout_sec = atoi(optarg); break;
            case 'o': output_path = optarg;       break;
            case 'O': binary_path = optarg;       break;
            default : usage(argv[0]);
        }
    }

    /* --- Validate mandatory parameters and lower bounds. --- */
    if (!config_file || !filter_file || !keyword_str || !output_path || !binary_path)
    {
        fprintf(stderr, "Hata: zorunlu parametreler eksik\n");
        usage(argv[0]);
    }
    if (t_readers < 1)
    {
        fprintf(stderr, "Hata: -t (reader_threads) >= 1 olmalı\n");
        exit(1);
    }
    if (w_workers < 1 || w_workers > MAX_WORKERS)
    {
        fprintf(stderr, "Hata: -w (worker_threads) 1..%d arasında olmalı\n", MAX_WORKERS);
        exit(1);
    }
    if (cap_a < 4)
    {
        fprintf(stderr, "Hata: -a >= 4 olmalı\n");
        exit(1);
    }
    if (cap_b < 4)
    {
        fprintf(stderr, "Hata: -b >= 4 olmalı\n");
        exit(1);
    }
    if (cap_d < 2)
    {
        fprintf(stderr, "Hata: -d >= 2 olmalı\n");
        exit(1);
    }
    if (timeout_sec < 1)
    {
        fprintf(stderr, "Hata: -T >= 1 olmalı\n");
        exit(1);
    }

    g_cap_b = cap_b;
    g_cap_d = cap_d;

    /* --- Read config and filter files. --- */
    int    n_files;
    int    n_sources;
    int    n_keywords;
    char** log_files = read_lines    (config_file, &n_files);
    char** p_sources = read_lines    (filter_file, &n_sources);
    char** keywords  = parse_keywords(keyword_str, &n_keywords);

    if (n_files < 1)
    {
        fprintf(stderr, "Hata: config dosyasında log dosyası yok\n");
        exit(1);
    }
    if (n_files > MAX_READERS)
    {
        fprintf(stderr, "Hata: en fazla %d log dosyası desteklenir\n", MAX_READERS);
        exit(1);
    }
    if (n_keywords < 1 || n_keywords > MAX_KEYWORDS)
    {
        fprintf(stderr, "Hata: 1..%d keyword olmalı\n", MAX_KEYWORDS);
        exit(1);
    }

    printf("[PID:%d] Parent started. Files: %d, Keywords: %s\n", getpid(), n_files, keyword_str);
    fflush(stdout);

    /* --- Create every shared region BEFORE forking, so child processes inherit the same mappings. --- */
    g_region_a = shm_create_region_a(cap_a);
    g_region_a->total_readers = n_files;
    for (int i = 0; i < LEVEL_COUNT; i++)
        g_region_b[i] = shm_create_region_b(cap_b);

    g_region_c = shm_create_region_c();
    g_region_d = shm_create_region_d(cap_d);

    printf("[PID:%d] Shared memory initialized (A:%d B:%dx4 D:%d).\n", getpid(), cap_a, cap_b, cap_d);
    fflush(stdout);

    /* SIGINT handler. SA_RESTART deliberately omitted so that blocked syscalls (cond_wait, waitpid) wake with EINTR.*/
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    /*One pipe per Reader, used for Watchdog heartbeats.*/
    int pipe_read_ends [MAX_READERS];
    int pipe_write_ends[MAX_READERS];
    for (int i = 0; i < n_files; i++)
    {
        int pfd[2];
        if (pipe(pfd) < 0)
        {
            perror("pipe");
            exit(1);
        }
        pipe_read_ends [i] = pfd[0];
        pipe_write_ends[i] = pfd[1];
    }

    /* --- 1. Fork Reader processes. --- */
    reader_proc_arg_t reader_args[MAX_READERS];
    for (int i = 0; i < n_files; i++)
    {
        reader_args[i].reader_idx      = i;
        reader_args[i].filepath        = log_files[i];
        reader_args[i].n_threads       = t_readers;
        reader_args[i].pipe_write_fd   = pipe_write_ends[i];
        reader_args[i].region_a        = g_region_a;
        reader_args[i].n_readers_total = n_files;

        printf("[PID:%d] Forking Reader %d -> %s\n", getpid(), i, log_files[i]);
        fflush(stdout);

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork reader");
            exit(1);
        }
        if (pid == 0)
        {
            /* Child: close the read ends (parent watchdog uses them) and the OTHER readers' write ends (only writes its own). */
            for (int j = 0; j < n_files; j++)
            {
                close(pipe_read_ends[j]);
                if (j != i)
                    close(pipe_write_ends[j]);
            }
            reader_process_main(&reader_args[i]);
            _exit(EXIT_SUCCESS);
        }
        register_child(pid);
        /* Parent does not write into pipes, so close the write end. */
        close(pipe_write_ends[i]);
    }

    /* --- 2. Fork Dispatcher. --- */
    printf("[PID:%d] Forking Dispatcher\n", getpid());
    fflush(stdout);
    {
        dispatcher_arg_t darg =
        {
            .region_a           = g_region_a,
            .region_c           = g_region_c,
            .region_d           = g_region_d,
            .priority_sources   = p_sources,
            .n_priority_sources = n_sources,
            .timeout_sec        = timeout_sec,
            .n_readers          = n_files,
        };
        for (int i = 0; i < LEVEL_COUNT; i++)
            darg.region_b[i] = g_region_b[i];

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork dispatcher");
            exit(1);
        }
        if (pid == 0)
        {
            for (int i = 0; i < n_files; i++)
                close(pipe_read_ends[i]);
            dispatcher_process_main(&darg);
            _exit(EXIT_SUCCESS);
        }
        register_child(pid);
    }

    /* --- 3. Fork four Analyzer processes (one per level). --- */
    for (int lvl = 0; lvl < LEVEL_COUNT; lvl++)
    {
        printf("[PID:%d] Forking Analyzer %s (index %d)\n",getpid(), LEVEL_NAMES[lvl], lvl);
        fflush(stdout);

        analyzer_arg_t aarg =
        {
            .level_idx  = lvl,
            .n_workers  = w_workers,
            .region_b   = g_region_b[lvl],
            .region_c   = g_region_c,
            .keywords   = keywords,
            .n_keywords = n_keywords,
        };

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork analyzer");
            exit(1);
        }
        if (pid == 0)
        {
            for (int i = 0; i < n_files; i++)
                close(pipe_read_ends[i]);
            analyzer_process_main(&aarg);
            _exit(EXIT_SUCCESS);
        }
        register_child(pid);
    }

    /* --- 4. Fork Aggregator. --- */
    printf("[PID:%d] Forking Aggregator\n", getpid());
    fflush(stdout);
    {
        aggregator_arg_t aarg =
        {
            .region_c    = g_region_c,
            .region_d    = g_region_d,
            .keywords    = keywords,
            .n_keywords  = n_keywords,
            .n_files     = n_files,
            .n_workers   = w_workers,
            .timeout_sec = timeout_sec,
            .output_path = output_path,
            .binary_path = binary_path,
            .filter_path = filter_file,
        };

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork aggregator");
            exit(1);
        }
        if (pid == 0)
        {
            for (int i = 0; i < n_files; i++)
                close(pipe_read_ends[i]);
            aggregator_process_main(&aarg);
            _exit(EXIT_SUCCESS);
        }
        register_child(pid);
    }

    /* --- 5. Spawn the Watchdog thread inside the parent. --- */
    g_children_alive = n_children;

    watchdog_arg_t warg;
    memset(&warg, 0, sizeof(warg));
    warg.n_readers      = n_files;
    warg.children_alive = &g_children_alive;
    for (int i = 0; i < n_files; i++)
    {
        warg.pipe_read_ends[i] = pipe_read_ends[i];
        strncpy(warg.log_names[i], log_files[i], 255);
        warg.log_names[i][255] = '\0';
    }

    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, watchdog_thread_func, &warg) != 0)
    {
        perror("pthread_create watchdog");
        exit(1);
    }
    printf("[PID:%d] Watchdog thread started.\n", getpid());
    fflush(stdout);

    /* --- 6. waitpid loop with SIGINT handling. --- */
    int children_alive = n_children;
    while (children_alive > 0)
    {
        if (sigint_received)
        {
            /* SIGINT path: send SIGTERM to all children, wait up to 5 seconds for them to exit, then forcibly clean up. */
            for (int i = 0; i < n_children; i++)
                kill(child_pids[i], SIGTERM);

            time_t deadline = time(NULL) + 5;
            while (children_alive > 0 && time(NULL) < deadline)
            {
                int   st;
                pid_t p = waitpid(-1, &st, WNOHANG);
                if (p > 0)
                {
                    children_alive--;
                    g_children_alive = children_alive;
                }
                else
                    usleep(20000);
            }
            shutdown_watchdog = 1;
            pthread_join(watchdog_tid, NULL);
            for (int i = 0; i < n_files; i++)
                close(pipe_read_ends[i]);
            cleanup_and_exit(1);
        }

        int   st;
        pid_t p = waitpid(-1, &st, WNOHANG);
        if (p > 0)
        {
            children_alive--;
            g_children_alive = children_alive;
        }
        else if (p == 0)
        {
            /* No child reaped; sleep briefly so this is not a busy spin (the assignment forbids tight polling loops). */
            usleep(20000);
        }
        else if (errno == ECHILD)
            break;
        else if (errno != EINTR)
        {
            perror("waitpid");
            break;
        }
    }

    /* --- 7. Stop the Watchdog and close pipes. --- */
    shutdown_watchdog = 1;
    pthread_join(watchdog_tid, NULL);
    for (int i = 0; i < n_files; i++)
        close(pipe_read_ends[i]);

    /* --- 8. Print the final SYSTEM SUMMARY block. --- */
    double total_w = 0.0;
    long   total_e = 0;
    for (int i = 0; i < LEVEL_COUNT; i++)
    {
        total_w += g_region_c->results[i].total_weighted_score;
        total_e += g_region_c->results[i].total_entries;
    }

    printf("\n==================================================\n");
    printf("SYSTEM SUMMARY\n");
    printf("Keywords      : %s\n",  keyword_str);
    printf("Log files     : %d\n",  n_files);
    printf("Total entries : %ld\n", total_e);
    printf("Total weighted: %.1f\n", total_w);
    printf("Filter file   : %s\n",  filter_file);
    for (int i = 0; i < LEVEL_COUNT; i++)
    {
        printf("  %-5s : %ld entries, score: %.1f\n", g_region_c->results[i].level, g_region_c->results[i].total_entries,g_region_c->results[i].total_weighted_score);
    }
    printf("==================================================\n");
    printf("Program terminated successfully.\n");
    fflush(stdout);

    /* --- Final cleanup: shared regions and heap allocations. --- */
    shm_destroy_region_a(g_region_a);
    for (int i = 0; i < LEVEL_COUNT; i++)
        shm_destroy_region_b(g_region_b[i], cap_b);

    shm_destroy_region_c(g_region_c);
    shm_destroy_region_d(g_region_d, cap_d);

    for (int i = 0; i < n_files;    i++) free(log_files[i]);
    for (int i = 0; i < n_sources;  i++) free(p_sources[i]);
    for (int i = 0; i < n_keywords; i++) free(keywords [i]);
    free(log_files);
    free(p_sources);
    free(keywords);

    return 0;
}