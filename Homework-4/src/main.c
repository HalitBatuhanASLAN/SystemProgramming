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

/* ─── Globals ───────────────────────────────────────────────────────────── */
static volatile sig_atomic_t sigint_received = 0;
static pid_t child_pids[256];
static int   n_children = 0;

static shm_region_a_t* g_region_a     = NULL;
static shm_region_b_t* g_region_b[LEVEL_COUNT];
static shm_region_c_t* g_region_c     = NULL;
static shm_region_d_t* g_region_d     = NULL;
static int             g_cap_a        = 0;
static int             g_cap_b        = 0;
static int             g_cap_d        = 0;

/* ─── SIGINT handler ────────────────────────────────────────────────────── */
static void sigint_handler(int sig) {
    (void)sig;
    sigint_received = 1;
}

/* ─── Yardımcı: kayıt ──────────────────────────────────────────────────── */
static void register_child(pid_t pid) {
    child_pids[n_children++] = pid;
}

static void cleanup_and_exit(int code) {
    shutdown_watchdog = 1;
    if (g_region_a) shm_destroy_region_a(g_region_a);
    for (int i = 0; i < LEVEL_COUNT; i++)
        if (g_region_b[i]) shm_destroy_region_b(g_region_b[i], g_cap_b);
    if (g_region_c) shm_destroy_region_c(g_region_c);
    if (g_region_d) shm_destroy_region_d(g_region_d, g_cap_d);
    _exit(code);
}

/* ─── Config okuma ──────────────────────────────────────────────────────── */
static char** read_lines(const char* path, int* out_count) {
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); exit(EXIT_FAILURE); }
    char** lines = NULL;
    int count = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        size_t l = strlen(buf);
        while (l > 0 && (buf[l-1]=='\n'||buf[l-1]=='\r')) buf[--l]='\0';
        if (l == 0) continue;
        lines = realloc(lines, (count+1) * sizeof(char*));
        lines[count++] = strdup(buf);
    }
    fclose(f);
    *out_count = count;
    return lines;
}

static char** parse_keywords(const char* kw_str, int* out_count) {
    char* dup = strdup(kw_str);
    char** kws = NULL;
    int count = 0;
    char* tok = strtok(dup, ",");
    while (tok && count < MAX_KEYWORDS) {
        kws = realloc(kws, (count+1) * sizeof(char*));
        kws[count++] = strdup(tok);
        tok = strtok(NULL, ",");
    }
    free(dup);
    *out_count = count;
    return kws;
}

/* ─── Kullanım ──────────────────────────────────────────────────────────── */
static void usage(const char* prog) {
    fprintf(stderr,
        "Kullanim: %s -c <config> -f <filter> -k <keywords>\n"
        "  -t <reader_threads> -w <worker_threads>\n"
        "  -a <cap_A> -b <cap_B> -d <cap_D>\n"
        "  -T <timeout> -o <output> -O <binary>\n", prog);
    exit(EXIT_FAILURE);
}

/* ─── MAIN ──────────────────────────────────────────────────────────────── */
int main(int argc, char* argv[]) {
    /* Varsayılanlar */
    const char* config_file  = NULL;
    const char* filter_file  = NULL;
    const char* keyword_str  = NULL;
    int         t_readers    = 2;
    int         w_workers    = 2;
    int         cap_a        = 64;
    int         cap_b        = 32;
    int         cap_d        = 16;
    int         timeout_sec  = 10;
    const char* output_path  = NULL;
    const char* binary_path  = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "c:f:k:t:w:a:b:d:T:o:O:")) != -1) {
        switch (opt) {
        case 'c': config_file  = optarg; break;
        case 'f': filter_file  = optarg; break;
        case 'k': keyword_str  = optarg; break;
        case 't': t_readers    = atoi(optarg); break;
        case 'w': w_workers    = atoi(optarg); break;
        case 'a': cap_a        = atoi(optarg); break;
        case 'b': cap_b        = atoi(optarg); break;
        case 'd': cap_d        = atoi(optarg); break;
        case 'T': timeout_sec  = atoi(optarg); break;
        case 'o': output_path  = optarg; break;
        case 'O': binary_path  = optarg; break;
        default:  usage(argv[0]);
        }
    }

    /* Doğrulama */
    if (!config_file||!filter_file||!keyword_str||!output_path||!binary_path) {
        fprintf(stderr, "Hata: zorunlu parametreler eksik\n"); usage(argv[0]);
    }
    if (t_readers < 1 || w_workers < 1 || w_workers > MAX_WORKERS)
        { fprintf(stderr, "Hata: gecersiz thread sayisi\n"); exit(1); }
    if (cap_a < 4 || cap_b < 4 || cap_d < 2)
        { fprintf(stderr, "Hata: gecersiz buffer kapasitesi\n"); exit(1); }
    if (timeout_sec < 1)
        { fprintf(stderr, "Hata: gecersiz timeout\n"); exit(1); }

    g_cap_a = cap_a; g_cap_b = cap_b; g_cap_d = cap_d;

    /* Config ve filter oku */
    int    n_files, n_sources, n_keywords;
    char** log_files  = read_lines(config_file, &n_files);
    char** p_sources  = read_lines(filter_file, &n_sources);
    char** keywords   = parse_keywords(keyword_str, &n_keywords);

    printf("[PID:%d] Parent started. Files: %d, Keywords: %s\n",
           getpid(), n_files, keyword_str);
    fflush(stdout);

    /* Shared memory oluştur */
    g_region_a = shm_create_region_a(cap_a);
    g_region_a->total_readers = n_files;
    for (int i = 0; i < LEVEL_COUNT; i++)
        g_region_b[i] = shm_create_region_b(cap_b);
    g_region_c = shm_create_region_c();
    g_region_d = shm_create_region_d(cap_d);

    printf("[PID:%d] Shared memory initialized (A:%d B:%dx4 D:%d).\n",
           getpid(), cap_a, cap_b, cap_d);
    fflush(stdout);

    /* SIGINT handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* Pipe'lar (Watchdog için) */
    int pipe_read_ends[64], pipe_write_ends[64];
    for (int i = 0; i < n_files; i++) {
        int pfd[2];
        if (pipe(pfd) < 0) { perror("pipe"); exit(1); }
        pipe_read_ends[i]  = pfd[0];
        pipe_write_ends[i] = pfd[1];
    }

    /* ── 1. Reader Süreçleri ─────────────────────────────────────────────── */
    reader_proc_arg_t reader_args[64];
    for (int i = 0; i < n_files; i++) {
        reader_args[i].reader_idx     = i;
        reader_args[i].filepath       = log_files[i];
        reader_args[i].n_threads      = t_readers;
        reader_args[i].pipe_write_fd  = pipe_write_ends[i];
        reader_args[i].region_a       = g_region_a;
        reader_args[i].n_readers_total= n_files;

        printf("[PID:%d] Forking Reader %d -> %s\n",
               getpid(), i, log_files[i]);
        fflush(stdout);

        pid_t pid = fork();
        if (pid < 0) { perror("fork reader"); exit(1); }
        if (pid == 0) {
            /* Child: pipe okuma uçlarını kapat */
            for (int j = 0; j < n_files; j++) {
                close(pipe_read_ends[j]);
                if (j != i) close(pipe_write_ends[j]);
            }
            reader_process_main(&reader_args[i]);
            exit(EXIT_SUCCESS); /* ulaşılmaz */
        }
        register_child(pid);
        close(pipe_write_ends[i]); /* parent yazmaz */
    }

    /* ── 2. Dispatcher ───────────────────────────────────────────────────── */
    printf("[PID:%d] Forking Dispatcher\n", getpid()); fflush(stdout);
    {
        dispatcher_arg_t darg = {
            .region_a         = g_region_a,
            .region_c         = g_region_c,
            .region_d         = g_region_d,
            .priority_sources = p_sources,
            .n_priority_sources = n_sources,
            .timeout_sec      = timeout_sec,
            .n_readers        = n_files,
        };
        for (int i = 0; i < LEVEL_COUNT; i++) darg.region_b[i] = g_region_b[i];

        pid_t pid = fork();
        if (pid < 0) { perror("fork dispatcher"); exit(1); }
        if (pid == 0) {
            for (int i = 0; i < n_files; i++) close(pipe_read_ends[i]);
            dispatcher_process_main(&darg);
        }
        register_child(pid);
    }

    /* ── 3. Analyzer Süreçleri ───────────────────────────────────────────── */
    for (int lvl = 0; lvl < LEVEL_COUNT; lvl++) {
        printf("[PID:%d] Forking Analyzer %s (index %d)\n",
               getpid(), LEVEL_NAMES[lvl], lvl);
        fflush(stdout);

        analyzer_arg_t aarg = {
            .level_idx  = lvl,
            .n_workers  = w_workers,
            .region_b   = g_region_b[lvl],
            .region_c   = g_region_c,
            .keywords   = keywords,
            .n_keywords = n_keywords,
        };

        pid_t pid = fork();
        if (pid < 0) { perror("fork analyzer"); exit(1); }
        if (pid == 0) {
            for (int i = 0; i < n_files; i++) close(pipe_read_ends[i]);
            analyzer_process_main(&aarg);
        }
        register_child(pid);
    }

    /* ── 4. Aggregator ───────────────────────────────────────────────────── */
    printf("[PID:%d] Forking Aggregator\n", getpid()); fflush(stdout);
    {
        aggregator_arg_t aarg = {
            .region_c   = g_region_c,
            .region_d   = g_region_d,
            .keywords   = keywords,
            .n_keywords = n_keywords,
            .n_files    = n_files,
            .timeout_sec= timeout_sec,
            .output_path= output_path,
            .binary_path= binary_path,
            .filter_path= filter_file,
        };

        pid_t pid = fork();
        if (pid < 0) { perror("fork aggregator"); exit(1); }
        if (pid == 0) {
            for (int i = 0; i < n_files; i++) close(pipe_read_ends[i]);
            aggregator_process_main(&aarg);
        }
        register_child(pid);
    }

    /* ── 5. Watchdog Thread ──────────────────────────────────────────────── */
    watchdog_arg_t warg;
    warg.n_readers = n_files;
    for (int i = 0; i < n_files; i++) {
        warg.pipe_read_ends[i] = pipe_read_ends[i];
        strncpy(warg.log_names[i], log_files[i], 255);
    }

    pthread_t watchdog_tid;
    if (pthread_create(&watchdog_tid, NULL, watchdog_thread_func, &warg) != 0) {
        perror("pthread_create watchdog"); exit(1);
    }
    printf("[PID:%d] Watchdog thread started.\n", getpid()); fflush(stdout);

    /* ── 6. waitpid döngüsü ──────────────────────────────────────────────── */
    int children_alive = n_children;
    while (children_alive > 0) {
        if (sigint_received) {
            /* SIGTERM → tüm çocuklar */
            for (int i = 0; i < n_children; i++)
                kill(child_pids[i], SIGTERM);

            time_t deadline = time(NULL) + 5;
            while (children_alive > 0 && time(NULL) < deadline) {
                int st;
                pid_t p = waitpid(-1, &st, WNOHANG);
                if (p > 0) children_alive--;
                else usleep(20000);
            }
            shutdown_watchdog = 1;
            pthread_join(watchdog_tid, NULL);
            for (int i = 0; i < n_files; i++) close(pipe_read_ends[i]);
            cleanup_and_exit(1);
        }

        int st;
        pid_t p = waitpid(-1, &st, WNOHANG);
        if (p > 0) {
            children_alive--;
        } else if (p == 0) {
            usleep(20000);
        } else if (errno != ECHILD) {
            perror("waitpid");
            break;
        } else {
            break;
        }
    }

    /* ── 7. Watchdog'u durdur ────────────────────────────────────────────── */
    shutdown_watchdog = 1;
    pthread_join(watchdog_tid, NULL);
    for (int i = 0; i < n_files; i++) close(pipe_read_ends[i]);

    /* ── 8. Final özet ───────────────────────────────────────────────────── */
    double total_w = 0.0;
    long   total_e = 0;
    for (int i = 0; i < LEVEL_COUNT; i++) {
        total_w += g_region_c->results[i].total_weighted_score;
        total_e += g_region_c->results[i].total_entries;
    }

    printf("\n==================================================\n");
    printf("SYSTEM SUMMARY\n");
    printf("Keywords   : %s\n", keyword_str);
    printf("Log files  : %d\n", n_files);
    printf("Total entries : %ld\n", total_e);
    printf("Total weighted: %.1f\n", total_w);
    for (int i = 0; i < LEVEL_COUNT; i++) {
        printf("  %-5s : %ld entries, score: %.1f\n",
               g_region_c->results[i].level,
               g_region_c->results[i].total_entries,
               g_region_c->results[i].total_weighted_score);
    }
    printf("==================================================\n");
    printf("Program terminated successfully.\n");
    fflush(stdout);

    /* Temizlik */
    shm_destroy_region_a(g_region_a);
    for (int i = 0; i < LEVEL_COUNT; i++)
        shm_destroy_region_b(g_region_b[i], cap_b);
    shm_destroy_region_c(g_region_c);
    shm_destroy_region_d(g_region_d, cap_d);

    for (int i = 0; i < n_files; i++)  free(log_files[i]);
    for (int i = 0; i < n_sources; i++) free(p_sources[i]);
    for (int i = 0; i < n_keywords; i++) free(keywords[i]);
    free(log_files); free(p_sources); free(keywords);

    return 0;
}
