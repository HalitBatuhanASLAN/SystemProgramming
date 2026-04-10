/*
 * ============================================================
 * FILE: main.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Entry point and coordinator (parent process) for the
 *   multi-process word transportation and concurrent sorting
 *   system.
 *
 * RESPONSIBILITIES OF THE PARENT PROCESS:
 *   1. Parse and validate command-line arguments.
 *   2. Read the input word list from the input file.
 *   3. Allocate and initialise shared memory (SharedData).
 *   4. Initialise all synchronisation primitives.
 *   5. Install SIGINT / SIGTERM signal handlers.
 *   6. Fork all child processes (word-carriers, letter-carriers,
 *      sorting processes, delivery elevator, reposition elevator).
 *   7. Monitor shared memory until all words are completed.
 *   8. Signal and collect all child processes (no zombies).
 *   9. Write the final output file.
 *  10. Print a system summary and exit cleanly.
 *
 * IMPORTANT RULES:
 *   - Only the PARENT creates child processes.  No child is
 *     allowed to fork further.
 *   - All child processes run one of the five role functions
 *     defined in the other modules (word_carrier_run, etc.)
 *     and then call _exit() to avoid flushing shared state.
 *   - SIGINT (Ctrl+C) sets system_running=0, causing every
 *     process to exit its main loop and terminate cleanly.
 * ============================================================
 */

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

/*
 * g_shared_data – global pointer to shared memory.
 *
 * Declared global so the SIGINT signal handler can reach it
 * without needing it to be passed as a parameter (signal
 * handlers receive only the signal number).
 */
static SharedData *g_shared_data = NULL;

/* ============================================================
 * sigint_handler
 * ------------------------------------------------------------
 * Signal handler for SIGINT (Ctrl+C) and SIGTERM.
 *
 * Sets system_running = 0 in shared memory.  Because the flag
 * is declared volatile in SharedData, all processes that are
 * currently spinning on "while (data->system_running)" will
 * see the change on their next loop iteration and exit their
 * main loop.
 *
 * Declared with (void)sig to suppress the "unused parameter"
 * compiler warning while keeping the correct sa_handler
 * signature.
 * ============================================================ */
static void sigint_handler(int sig) {
    (void)sig; /* Signal number not needed; we handle all signals the same way. */
    if (g_shared_data) {
        g_shared_data->system_running = 0;
    }
}

/* ============================================================
 * register_child
 * ------------------------------------------------------------
 * Appends a child PID to the parent's tracking list so that
 * cleanup_children() can later send signals and collect them
 * with waitpid().
 *
 * Protected by children_mutex because, in principle, multiple
 * threads could register children concurrently (though in this
 * single-threaded parent this is for safety/correctness only).
 * ============================================================ */
static void register_child(SharedData *data, pid_t pid) {
    pthread_mutex_lock(&data->children_mutex);
    if (data->num_children < MAX_PROCESSES) {
        data->child_pids[data->num_children++] = pid;
    }
    pthread_mutex_unlock(&data->children_mutex);
}

/* ============================================================
 * cleanup_children
 * ------------------------------------------------------------
 * Sends SIGTERM to every child, waits briefly, then sends
 * SIGKILL to any that are still alive, and finally collects
 * all of them with waitpid() to prevent zombie processes.
 *
 * Two-phase shutdown:
 *   Phase 1 – SIGTERM: gives children a chance to exit their
 *             main loop cleanly (they check system_running).
 *   Phase 2 – SIGKILL (after 100 ms): forcefully terminates
 *             any child that did not respond to SIGTERM.
 *   Phase 3 – waitpid(): reaps every child so no zombies remain.
 * ============================================================ */
static void cleanup_children(SharedData *data) {

    /* Phase 1: request graceful termination. */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            kill(data->child_pids[i], SIGTERM);
        }
    }

    /* Allow 100 ms for children to exit on their own. */
    usleep(100000);

    /* Phase 2: force-kill any survivors. */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            int status;
            pid_t result = waitpid(data->child_pids[i], &status, WNOHANG);
            if (result == 0) {
                /* Child is still running → SIGKILL. */
                kill(data->child_pids[i], SIGKILL);
            }
        }
    }

    /* Phase 3: reap every child (blocking wait, no zombies). */
    for (int i = 0; i < data->num_children; i++) {
        if (data->child_pids[i] > 0) {
            waitpid(data->child_pids[i], NULL, 0);
        }
    }
}

/* ============================================================
 * print_summary
 * ------------------------------------------------------------
 * Prints the final system statistics to stdout after all words
 * have been sorted (or after a SIGINT interruption).
 *
 * Reported values:
 *   - Total words in the input file.
 *   - Words successfully completed (sorted).
 *   - Total admission retries (capacity rejections).
 *   - Total characters transported by letter-carriers.
 *   - Delivery and reposition elevator operation counts.
 * ============================================================ */
static void print_summary(SharedData *data) {
    printf("\n--------------------------------------------------\n");
    printf("All words have been transported and sorted...\n");
    printf("Output file is being created...\n\n");
    printf("System Summary:\n");
    printf("  Total words: %d\n",                    data->total_words);
    printf("  Completed words: %d\n",                data->completed_words);
    printf("  Retries: %d\n",                         data->total_retries);
    printf("  Characters transported: %d\n",          data->total_chars_transported);
    printf("  Delivery elevator operations: %d\n",    data->delivery_elevator_ops);
    printf("  Reposition elevator operations: %d\n",  data->reposition_elevator_ops);
    printf("\nProgram terminated successfully.\n");
}

/* ============================================================
 * check_all_completed
 * ------------------------------------------------------------
 * Returns 1 if every word in shared memory has completed == 1,
 * meaning the sorting process has fully reconstructed it.
 * Returns 0 if any word is still pending.
 *
 * Called by the parent's monitoring loop (50 ms polling) and
 * again after cleanup to decide whether to write the output.
 * ============================================================ */
static int check_all_completed(SharedData *data) {
    for (int i = 0; i < data->total_words; i++) {
        if (!data->words[i].completed) return 0;
    }
    return 1;
}

/* ============================================================
 * main
 * ============================================================ */
int main(int argc, char *argv[]) {
    SystemConfig config;
    WordInfo     temp_words[MAX_WORDS]; /* Temporary stack buffer for file I/O. */
    int          word_count;
    pid_t        pid;
    int          global_carrier_id = 0; /* Unique ID counter for all child procs. */

    printf("Program is starting...\n");

    /* ── Step 1: Parse command-line arguments ─────────────── */
    if (parse_args(argc, argv, &config) != 0) {
        return EXIT_FAILURE;
    }

    /* ── Step 2: Read input file ──────────────────────────── */
    printf("Input file is being read...\n");
    word_count = read_input_file(config.input_file, temp_words, MAX_WORDS);
    if (word_count < 0) {
        return EXIT_FAILURE;
    }

    /*
     * Validate that each word's sorting_floor is within the
     * configured range [0, num_floors - 1].  This check must
     * happen BEFORE shared memory is allocated so failures are
     * reported cleanly without resource leaks.
     */
    for (int i = 0; i < word_count; i++) {
        if (temp_words[i].sorting_floor < 0 ||
            temp_words[i].sorting_floor >= config.num_floors) {
            fprintf(stderr,
                    "Error: Word '%s' has invalid sorting_floor %d (max: %d)\n",
                    temp_words[i].word,
                    temp_words[i].sorting_floor,
                    config.num_floors - 1);
            return EXIT_FAILURE;
        }
    }

    /* ── Step 3: Create shared memory ────────────────────── */
    printf("Shared memory is initialized...\n");
    SharedData *data = shm_init(&config, temp_words, word_count);
    if (!data) {
        return EXIT_FAILURE;
    }
    g_shared_data = data; /* Make it reachable from the signal handler. */

    /* ── Step 4: Initialise synchronisation primitives ───── */
    printf("Synchronization primitives are created...\n");
    if (sync_init(data) != 0) {
        shm_destroy(data);
        return EXIT_FAILURE;
    }

    /* ── Step 5: Install signal handlers ─────────────────── */
    /*
     * sigaction() is preferred over signal() because its behaviour
     * is well-defined by POSIX (signal() semantics vary by OS).
     */
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

    /*
     * SIGTERM uses the same handler so that both Ctrl+C and
     * a programmatic kill() result in a clean shutdown.
     */
    struct sigaction sa_term;
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = sigint_handler;
    sigemptyset(&sa_term.sa_mask);
    if (sigaction(SIGTERM, &sa_term, NULL) == -1) {
        print_error("sigaction SIGTERM");
        /* Non-fatal: continue without SIGTERM handling. */
    }

    /* Seed the random number generator differently per process. */
    srand(time(NULL) ^ getpid());

    /* ── Step 6: Fork all child processes ────────────────── */
    printf("Processes are being created...\n");
    log_msg("Parent process started");

    /*
     * Create process groups per floor.
     * For each floor we create:
     *   - word_carriers_per_floor  word-carrier processes
     *   - letter_carriers_per_floor letter-carrier processes
     *   - sorting_processes_per_floor sorting processes
     *
     * global_carrier_id is incremented for every child so every
     * process has a unique identifier used in log messages and
     * per-process statistics arrays.
     */
    for (int floor = 0; floor < config.num_floors; floor++) {
        printf("--- Initializing Floor %d ---\n", floor);

        /* ── Word-carrier processes for this floor ─────────── */
        for (int w = 0; w < config.word_carriers_per_floor; w++) {
            pid = safe_fork();
            if (pid == 0) {
                /*
                 * CHILD: run as a word-carrier.
                 * Re-seed rand() with a different value from the
                 * parent to ensure each child makes independent
                 * random choices.
                 */
                srand(time(NULL) ^ getpid());
                word_carrier_run(data, floor, global_carrier_id);
                _exit(EXIT_SUCCESS); /* _exit avoids flushing stdio buffers. */
            }
            /* PARENT: record the child's PID and announce it. */
            register_child(data, pid);
            printf("[PID:%ld] Word-carrier-process_%d initialized on floor %d\n",
                   (long)pid, global_carrier_id, floor);
            global_carrier_id++;
        }

        /* ── Letter-carrier processes for this floor ────────── */
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

        /* ── Sorting processes for this floor ───────────────── */
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

    /* ── Delivery elevator process (one for the whole building) */
    pid = safe_fork();
    if (pid == 0) {
        delivery_elevator_run(data);
        _exit(EXIT_SUCCESS);
    }
    register_child(data, pid);
    printf("[PID:%ld] Delivery elevator process started\n", (long)pid);

    /* ── Reposition elevator process (one for the whole building) */
    pid = safe_fork();
    if (pid == 0) {
        reposition_elevator_run(data);
        _exit(EXIT_SUCCESS);
    }
    register_child(data, pid);
    printf("[PID:%ld] Reposition elevator process started\n", (long)pid);

    printf("--------------------------------------------------\n");

    /* ── Step 7: Monitor until completion ────────────────── */
    /*
     * The parent polls check_all_completed() every 50 ms.
     * When all words are sorted it sets system_running = 0
     * to signal every child to exit its loop.
     *
     * An alternative design would use pthread_cond_wait on
     * state_cond, but simple polling is sufficient here and
     * avoids the complexity of the parent holding a mutex while
     * sleeping (which would interfere with children that also
     * need state_mutex).
     */
    while (data->system_running) {
        if (check_all_completed(data)) {
            data->system_running = 0;
            break;
        }
        usleep(50000); /* 50 ms between checks. */
    }

    /* ── Step 8: Shut down all children ───────────────────── */
    cleanup_children(data);

    /* ── Step 9: Write output file ────────────────────────── */
    if (check_all_completed(data)) {
        /*
         * Normal completion: write the sorted output and print
         * the full summary.
         */
        if (write_output_file(config.output_file, data->words,
                               data->total_words) != 0) {
            fprintf(stderr, "Error: Failed to write output file\n");
        }
        data->completed_words = data->total_words;
        print_summary(data);

    } else {
        /*
         * Interrupted by SIGINT before completion.
         * Count how many words did finish and print a partial
         * summary so the user knows what happened.
         */
        int completed = 0;
        for (int i = 0; i < data->total_words; i++) {
            if (data->words[i].completed) completed++;
        }
        data->completed_words = completed;
        printf("\nSystem interrupted. Partial results:\n");
        print_summary(data);
    }

    /* ── Step 10: Release all resources ───────────────────── */
    /*
     * Destroy synchronisation primitives first (before the
     * shared memory region disappears) then unmap the region.
     */
    sync_destroy(data);
    shm_destroy(data);

    return EXIT_SUCCESS;
}
