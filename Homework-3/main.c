#include "common.h"
#include "argument_parsing.h"
#include "file_issues.h"
#include "shared_memory.h"
#include "mutex_semaphore.h"
#include "utils.h"
#include "word_carrier_process.h"
#include "letter_carrier_process.h"
#include "sorting_process.h"
#include "elevator_process.h"

/* Global pointer - SIGINT handler'dan erisilebilmesi icin */
static SharedData *g_shared_data = NULL;

/* SIGINT handler: temiz sonlandirma */
static void sigint_handler(int sig) {
    (void)sig;
    if (g_shared_data) {
        g_shared_data->system_running = 0;  /* Tum process'lere dur sinyali */
    }
}

/* Child PID'ini listeye ekle */
static void register_child(SharedData *data, pid_t pid) {
    pthread_mutex_lock(&data->children_mutex);
    if (data->num_children < MAX_PROCESSES) {
        data->child_pids[data->num_children++] = pid;
    }
    pthread_mutex_unlock(&data->children_mutex);
}

/* Tum child process'leri sonlandir ve bekle */
static void cleanup_children(SharedData *data) {
    /* Once SIGTERM gonder */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            kill(data->child_pids[i], SIGTERM);
        }
    }

    /* Kisa sure bekle */
    usleep(100000); /* 100ms */

    /* Hala yasayan varsa SIGKILL */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            int status;
            pid_t result = waitpid(data->child_pids[i], &status, WNOHANG);
            if (result == 0) {
                /* Hala calisiyor, SIGKILL gonder */
                kill(data->child_pids[i], SIGKILL);
            }
        }
    }

    /* Tum child'lari topla (zombie onleme) */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            waitpid(data->child_pids[i], NULL, 0);
        }
    }
}

/* Sistem ozetini terminale yazdir */
static void print_summary(SharedData *data) {
    printf("\n--------------------------------------------------\n");
    printf("All words have been transported and sorted...\n");
    printf("Output file is being created...\n\n");
    printf("System Summary:\n");
    printf("  Total words: %d\n", data->total_words);
    printf("  Completed words: %d\n", data->completed_words);
    printf("  Retries: %d\n", data->total_retries);
    printf("  Characters transported: %d\n", data->total_chars_transported);
    printf("  Delivery elevator operations: %d\n", data->delivery_elevator_ops);
    printf("  Reposition elevator operations: %d\n", data->reposition_elevator_ops);
    printf("  Per-process statistics:\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (data->word_carrier_admissions[i] > 0) {
            printf("    Word-carrier-process_%d admissions: %d\n",
                   i, data->word_carrier_admissions[i]);
        }
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (data->letter_carrier_transports[i] > 0) {
            printf("    Letter-carrier-process_%d transports: %d\n",
                   i, data->letter_carrier_transports[i]);
        }
    }
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (data->sorting_process_completions[i] > 0) {
            printf("    Sorting-process_%d completions: %d\n",
                   i, data->sorting_process_completions[i]);
        }
    }
    printf("\nProgram terminated successfully.\n");
}

/* Tum kelimelerin tamamlanip tamamlanmadigini kontrol et */
static int check_all_completed(SharedData *data) {
    for (int i = 0; i < data->total_words; i++) {
        if (!data->words[i].completed) return 0;
    }
    return 1;
}

/* Hic letter-carrier kalmayan kat varsa parent yenilerini olusturur */
static void respawn_letter_carriers_if_needed(SharedData *data, int *global_carrier_id) {
    for (int floor = 0; floor < data->config.num_floors; floor++) {
        int should_spawn = 0;

        pthread_mutex_lock(&data->floors[floor].floor_mutex);
        if (data->system_running &&
            data->floors[floor].letter_carrier_count == 0 &&
            data->floors[floor].active_word_count > 0) {
            data->floors[floor].letter_carrier_count += data->config.letter_carriers_per_floor;
            should_spawn = 1;
        }
        pthread_mutex_unlock(&data->floors[floor].floor_mutex);

        if (!should_spawn) continue;

        for (int i = 0; i < data->config.letter_carriers_per_floor; i++) {
            pid_t pid = safe_fork();
            if (pid == 0) {
                srand(time(NULL) ^ getpid());
                letter_carrier_run(data, floor, *global_carrier_id);
                _exit(EXIT_SUCCESS);
            }
            register_child(data, pid);
            printf("[PID:%ld] Letter-carrier-process_%d re-initialized on floor %d\n",
                   (long)pid, *global_carrier_id, floor);
            (*global_carrier_id)++;
        }
    }
}

int main(int argc, char *argv[]) {
    SystemConfig config;
    WordInfo temp_words[MAX_WORDS];
    int word_count;
    pid_t pid;
    int global_carrier_id = 0;

    printf("Program is starting...\n");

    /* 1. Argumanlari parse et */
    if (parse_args(argc, argv, &config) != 0) {
        return EXIT_FAILURE;
    }

    /* 2. Input dosyasini oku */
    printf("Input file is being read...\n");
    word_count = read_input_file(config.input_file, temp_words, MAX_WORDS);
    if (word_count < 0) {
        return EXIT_FAILURE;
    }

    /* Sorting floor validasyonu */
    for (int i = 0; i < word_count; i++) {
        if (temp_words[i].sorting_floor < 0 || temp_words[i].sorting_floor >= config.num_floors) {
            fprintf(stderr, "Error: Word '%s' has invalid sorting_floor %d (max: %d)\n",
                    temp_words[i].word, temp_words[i].sorting_floor, config.num_floors - 1);
            return EXIT_FAILURE;
        }
    }

    /* 3. Shared memory olustur */
    printf("Shared memory is initialized...\n");
    SharedData *data = shm_init(&config, temp_words, word_count);
    if (!data) {
        return EXIT_FAILURE;
    }
    g_shared_data = data;

    /* 4. Senkronizasyon primitive'lerini olustur */
    printf("Synchronization primitives are created...\n");
    if (sync_init(data) != 0) {
        shm_destroy(data);
        return EXIT_FAILURE;
    }

    /* 5. SIGINT handler kur */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        print_error("sigaction SIGINT");
        sync_destroy(data);
        shm_destroy(data);
        return EXIT_FAILURE;
    }

    /* SIGTERM handler (child'lar icin) */
    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigint_handler; /* Ayni handler: system_running = 0 */
    sigemptyset(&sa_term.sa_mask);
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        print_error("sigaction SIGTERM");
    }

    /* Rastgele sayi ureteci seed */
    srand(time(NULL) ^ getpid());

    /* 6. Process'leri olustur */
    printf("Processes are being created...\n");
    log_msg("Parent process started");

    /* Her kat icin process'leri fork et */
    for (int floor = 0; floor < config.num_floors; floor++) {
        printf("--- Initializing Floor %d ---\n", floor);

        /* Word-carrier process'leri */
        for (int w = 0; w < config.word_carriers_per_floor; w++) {
            pid = safe_fork();
            if (pid == 0) {
                /* Child: word-carrier olarak calis */
                srand(time(NULL) ^ getpid());
                word_carrier_run(data, floor, global_carrier_id);
                _exit(EXIT_SUCCESS);
            }
            register_child(data, pid);
            printf("[PID:%ld] Word-carrier-process_%d initialized on floor %d\n",
                   (long)pid, global_carrier_id, floor);
            global_carrier_id++;
        }

        /* Letter-carrier process'leri */
        for (int l = 0; l < config.letter_carriers_per_floor; l++) {
            pid = safe_fork();
            if (pid == 0) {
                srand(time(NULL) ^ getpid());
                letter_carrier_run(data, floor, global_carrier_id);
                _exit(EXIT_SUCCESS);
            }
            register_child(data, pid);
            printf("[PID:%ld] Letter-carrier-process_%d initialized on floor %d\n",
                   (long)pid, global_carrier_id, floor);
            global_carrier_id++;
        }

        /* Sorting process'leri */
        for (int s = 0; s < config.sorting_processes_per_floor; s++) {
            pid = safe_fork();
            if (pid == 0) {
                srand(time(NULL) ^ getpid());
                sorting_process_run(data, floor, global_carrier_id);
                _exit(EXIT_SUCCESS);
            }
            register_child(data, pid);
            printf("[PID:%ld] Sorting-process_%d initialized on floor %d\n",
                   (long)pid, global_carrier_id, floor);
            global_carrier_id++;
        }
    }

    /* Delivery elevator process */
    pid = safe_fork();
    if (pid == 0) {
        delivery_elevator_run(data);
        _exit(EXIT_SUCCESS);
    }
    register_child(data, pid);
    printf("[PID:%ld] Delivery elevator process started\n", (long)pid);

    /* Reposition elevator process */
    pid = safe_fork();
    if (pid == 0) {
        reposition_elevator_run(data);
        _exit(EXIT_SUCCESS);
    }
    register_child(data, pid);
    printf("[PID:%ld] Reposition elevator process started\n", (long)pid);

    printf("--------------------------------------------------\n");

    /* 7. Parent: tamamlanmayi bekle */
    pthread_mutex_lock(&data->state_mutex);
    while (data->system_running) {
        if (check_all_completed(data)) {
            data->system_running = 0;
            pthread_cond_broadcast(&data->state_cond);
            break;
        }

        pthread_mutex_unlock(&data->state_mutex);
        respawn_letter_carriers_if_needed(data, &global_carrier_id);
        pthread_mutex_lock(&data->state_mutex);

        if (!data->system_running || check_all_completed(data)) {
            continue;
        }

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000; /* 100ms */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&data->state_cond, &data->state_mutex, &ts);
    }
    pthread_mutex_unlock(&data->state_mutex);

    /* 8. Tum child'lari durdur ve topla */
    cleanup_children(data);

    /* 9. Output dosyasini yaz */
    if (check_all_completed(data)) {
        if (write_output_file(config.output_file, data->words, data->total_words) != 0) {
            fprintf(stderr, "Error: Failed to write output file\n");
        }
        data->completed_words = data->total_words;
        print_summary(data);
    } else {
        /* SIGINT ile kesildi - partial summary */
        int completed = 0;
        for (int i = 0; i < data->total_words; i++) {
            if (data->words[i].completed) completed++;
        }
        data->completed_words = completed;
        printf("\nSystem interrupted. Partial results:\n");
        print_summary(data);
    }

    /* 10. Cleanup */
    sync_destroy(data);
    shm_destroy(data);

    return EXIT_SUCCESS;
}
