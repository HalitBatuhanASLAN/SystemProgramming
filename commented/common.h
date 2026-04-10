/*
 * ============================================================
 * FILE: common.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   This is the central header file shared by every module in
 *   the system.  It defines:
 *     - All compile-time constants (array limits, elevator
 *       direction codes, etc.)
 *     - Every data structure that lives in shared memory
 *     - Standard library includes required across the project
 *
 * WHY A SINGLE SHARED HEADER?
 *   Because the system uses multiple independent processes that
 *   all map the SAME shared-memory region (SharedData), every
 *   process must agree on the exact memory layout.  Keeping all
 *   type definitions in one place guarantees that agreement.
 *
 * ARCHITECTURE OVERVIEW:
 *   Parent process
 *     ├── Word-carrier processes  (admit words to floors)
 *     ├── Letter-carrier processes (transport individual chars)
 *     ├── Sorting processes        (reconstruct words)
 *     ├── Delivery elevator process (move chars between floors)
 *     └── Reposition elevator process (relocate idle carriers)
 *
 *   All of the above communicate exclusively through the
 *   SharedData structure allocated with mmap(MAP_SHARED).
 * ============================================================
 */

#ifndef COMMON_H
#define COMMON_H

/*
 * Feature-test macros must be defined BEFORE any system header
 * is included so that the C library exposes the extended POSIX
 * symbols we need (sigaction, usleep, kill, clock_gettime, …).
 */
#define _POSIX_C_SOURCE 200809L   /* POSIX.1-2008 API surface    */
#define _GNU_SOURCE               /* GNU extensions (e.g. mmap)  */

/* ── Standard library headers ─────────────────────────────── */
#include <stdio.h>      /* printf, fprintf, fopen, …            */
#include <stdlib.h>     /* malloc, free, exit, atoi, …          */
#include <string.h>     /* memset, memcpy, strncpy, strerror, … */
#include <unistd.h>     /* fork, getpid, usleep, access, …      */
#include <sys/types.h>  /* pid_t, size_t, …                     */
#include <sys/wait.h>   /* waitpid, WNOHANG                     */
#include <sys/mman.h>   /* mmap, munmap, MAP_SHARED, …          */
#include <pthread.h>    /* pthread_mutex_t, pthread_cond_t, …   */
#include <semaphore.h>  /* sem_t, sem_init, sem_post, …         */
#include <signal.h>     /* sigaction, SIGINT, SIGTERM, …        */
#include <fcntl.h>      /* O_RDONLY, open, …                    */
#include <errno.h>      /* errno, strerror                      */
#include <time.h>       /* clock_gettime, struct timespec        */


/* ============================================================
 * SECTION 1 – COMPILE-TIME CONSTANTS
 * ============================================================ */

/*
 * MAX_WORD_LEN – maximum number of characters in a single word
 * (including the NUL terminator).  Words longer than this are
 * rejected during input parsing.
 */
#define MAX_WORD_LEN      64

/*
 * MAX_WORDS – maximum total number of words the system can hold
 * at once.  This determines the size of the words[] array inside
 * SharedData.  The input file must contain no more than this
 * many entries.
 */
#define MAX_WORDS         256

/*
 * MAX_FLOORS – maximum number of building floors supported.
 * Passed via -f on the command line; must be in [1, MAX_FLOORS].
 */
#define MAX_FLOORS        64

/*
 * MAX_ELEVATOR_CAP – upper bound on the capacity of either
 * elevator (delivery or reposition).  Passed via -d / -r; must
 * not exceed this value.
 */
#define MAX_ELEVATOR_CAP  64

/*
 * MAX_QUEUE_SIZE – maximum number of pending requests that can
 * be queued for a single elevator at any moment.  If the queue
 * is full, new requests are silently dropped (the carrier will
 * retry when the elevator drains).
 */
#define MAX_QUEUE_SIZE    512

/*
 * MAX_PROCESSES – maximum total number of child processes the
 * parent can create and track.  Also used as the dimension of
 * per-process statistics arrays.
 */
#define MAX_PROCESSES     512

/* ── Elevator direction codes ──────────────────────────────── */
#define DIR_UP    1     /* Elevator is moving upward            */
#define DIR_DOWN -1     /* Elevator is moving downward          */
#define DIR_IDLE  0     /* Elevator has no pending requests     */


/* ============================================================
 * SECTION 2 – SYSTEM CONFIGURATION  (SystemConfig)
 * ============================================================
 *
 * Populated by parse_args() from command-line flags and then
 * copied into shared memory so every child process can read the
 * configuration without extra IPC.
 */
typedef struct {
    int num_floors;                   /* -f : total floors (>= 1)              */
    int word_carriers_per_floor;      /* -w : word-carrier procs per floor      */
    int letter_carriers_per_floor;    /* -l : letter-carrier procs per floor    */
    int sorting_processes_per_floor;  /* -s : sorting procs per floor           */
    int max_words_per_floor;          /* -c : max active words on any floor     */
    int delivery_elevator_capacity;   /* -d : max chars inside delivery elev.   */
    int reposition_elevator_capacity; /* -r : max carriers inside repos. elev.  */
    char input_file[256];             /* -i : path to input .txt file           */
    char output_file[256];            /* -o : path to output .txt file          */
} SystemConfig;


/* ============================================================
 * SECTION 3 – CHARACTER TASK  (CharTask)
 * ============================================================
 *
 * When a word is admitted into the system each of its characters
 * becomes an independent CharTask.  Letter-carrier processes
 * claim and deliver these tasks one by one.
 *
 * Example: word "apple" (word_id=101, arrival_floor=0,
 *          sorting_floor=2) produces five CharTask entries:
 *   { word_id=101, character='a', original_index=0, src=0, dest=2 }
 *   { word_id=101, character='p', original_index=1, src=0, dest=2 }
 *   … and so on.
 *
 * The original_index is critical for the sorting process to know
 * where each character belongs in the final reconstructed word.
 */
typedef struct {
    int  word_id;        /* ID of the word this character belongs to           */
    char character;      /* The actual character to be transported              */
    int  original_index; /* Position of this char in the original word [0..n)  */
    int  src_floor;      /* Floor where the word arrived (set on admission)     */
    int  dest_floor;     /* Sorting floor where this char must be delivered     */
    int  claimed;        /* 1 = a letter-carrier has reserved this task         */
    int  delivered;      /* 1 = the character has reached the sorting floor     */
} CharTask;


/* ============================================================
 * SECTION 4 – WORD INFORMATION  (WordInfo)
 * ============================================================
 *
 * The central data structure for a single word.  It lives in
 * shared memory so every process type can read and update it.
 *
 * LIFECYCLE OF A WORD:
 *   1. Read from input file → fields populated, all flags = 0.
 *   2. A word-carrier claims it  → claimed = 1.
 *   3. Both floors have capacity → admitted = 1, arrival_floor set.
 *      (If not, claimed is reset to 0 and the word is retried.)
 *   4. Letter-carriers pick up CharTask entries and transport chars.
 *   5. Sorting processes fill sorting_area[], set fixed[] flags.
 *   6. When all fixed[i] == 1    → completed = 1.
 *
 * SORTING DATA STRUCTURES:
 *   sorting_area[i]  – character currently sitting at slot i.
 *   occupied[i]      – 1 if slot i holds a character.
 *   fixed[i]         – 1 if slot i is permanently locked in place
 *                       (sorting_area[i] == word[i] and will not
 *                        change again).
 *
 * Characters are placed into the FIRST available non-fixed empty
 * slot, NOT directly into their correct index.  Sorting processes
 * then rearrange them using moves and swaps.
 *
 * CONCURRENCY:
 *   word_mutex protects all mutable fields of this struct.
 *   Only one sorting process may hold word_mutex at a time, so
 *   two sorters can never operate on the same word simultaneously.
 */
typedef struct {
    /* ── Identity ─────────────────────────────────────────── */
    int  word_id;                       /* Unique word identifier from input     */
    char word[MAX_WORD_LEN];            /* Original word string (NUL-terminated) */
    int  word_len;                      /* strlen(word)                           */
    int  sorting_floor;                 /* Floor where sorting must happen        */
    int  arrival_floor;                 /* Floor where the word was admitted (-1
                                           until a word-carrier sets it)          */

    /* ── Lifecycle flags ──────────────────────────────────── */
    int  claimed;                       /* Word is reserved by a word-carrier     */
    int  admitted;                      /* Word has been accepted into the system */
    int  completed;                     /* All characters sorted correctly        */

    /* ── Sorting state ────────────────────────────────────── */
    char sorting_area[MAX_WORD_LEN];    /* Characters placed here by carriers     */
    int  occupied[MAX_WORD_LEN];        /* occupied[i] = 1 means slot i has char  */
    int  fixed[MAX_WORD_LEN];           /* fixed[i] = 1 means slot i is finalised */

    /* ── Character tasks ──────────────────────────────────── */
    CharTask char_tasks[MAX_WORD_LEN];  /* One task per character                 */
    int      num_char_tasks;            /* == word_len                            */

    /* ── Per-word synchronisation ─────────────────────────── */
    pthread_mutex_t word_mutex;         /* Guards all mutable fields above        */
} WordInfo;


/* ============================================================
 * SECTION 5 – ELEVATOR REQUEST  (ElevatorRequest)
 * ============================================================
 *
 * Represents a single item in an elevator's request queue.
 *
 * served field state machine:
 *   0  = waiting to be picked up
 *   2  = currently inside the elevator (in transit)
 *   1  = delivered and ready for cleanup
 *
 * Requests with served == 1 are purged by clean_served_requests()
 * inside the elevator process before each movement cycle.
 */
typedef struct {
    int  requester_type;  /* 0 = letter-carrier (delivery elevator)
                             1 = idle carrier  (reposition elevator)   */
    int  from_floor;      /* Floor where the passenger is waiting       */
    int  to_floor;        /* Floor the passenger wants to reach         */
    int  carrier_id;      /* ID of the requesting letter-carrier        */
    int  word_id;         /* Word being transported (delivery only;
                             -1 for reposition requests)                */
    char character;       /* Character being transported (delivery only;
                             '\0' for reposition)                       */
    int  original_index;  /* Index in the word (delivery only; -1 reposition) */
    int  served;          /* 0=waiting, 2=inside elevator, 1=delivered  */
} ElevatorRequest;


/* ============================================================
 * SECTION 6 – ELEVATOR STATE  (ElevatorState)
 * ============================================================
 *
 * Runtime state of one elevator (delivery OR reposition).
 * Both elevators follow the same movement policy:
 *   - Direction-based (UP / DOWN / IDLE).
 *   - Serves all requests in the current direction before
 *     switching; mandatory reversal at top and bottom floors.
 *   - Becomes IDLE only when both the queue is empty AND the
 *     elevator is empty (current_load == 0).
 *
 * SYNCHRONISATION:
 *   elev_mutex  – protects queue[], queue_size, current_floor,
 *                 direction, current_load.
 *   elev_cond   – used by carrier processes to wait for their
 *                 request to be served (served flag changes).
 *   request_sem – posted by a carrier every time a new request
 *                 is enqueued; the elevator process blocks on
 *                 sem_timedwait() so it does not busy-wait.
 */
typedef struct {
    int current_floor;                     /* Floor the elevator is currently at  */
    int direction;                         /* DIR_UP, DIR_DOWN, or DIR_IDLE       */
    int capacity;                          /* Max passengers/chars at once        */
    int current_load;                      /* Number currently inside             */

    /* ── Request queue ────────────────────────────────────── */
    ElevatorRequest queue[MAX_QUEUE_SIZE]; /* Circular(ish) array of requests     */
    int             queue_size;            /* Active entries in queue[]           */

    /* ── Synchronisation primitives ──────────────────────── */
    pthread_mutex_t elev_mutex;            /* Protects all fields above           */
    pthread_cond_t  elev_cond;             /* Signals state changes to waiters    */
    sem_t           request_sem;           /* Wakes elevator when requests arrive */
} ElevatorState;


/* ============================================================
 * SECTION 7 – FLOOR  (Floor)
 * ============================================================
 *
 * Represents one floor of the building.
 *
 * active_word_count  tracks how many words are currently "active"
 * on this floor (either as their arrival floor or sorting floor).
 * It is used for admission control: a word can only be admitted
 * if this count is below max_words_per_floor for BOTH its arrival
 * floor and sorting floor.
 *
 * letter_carrier_count  is initialised to letter_carriers_per_floor
 * and updated whenever a carrier moves to a different floor.  If
 * it ever drops to zero the parent process must spawn new carriers
 * on that floor (as per the assignment requirement).
 */
typedef struct {
    int floor_id;               /* Floor number (0 … num_floors-1)              */
    int active_word_count;      /* Words currently active on this floor         */
    int letter_carrier_count;   /* Letter-carriers currently on this floor      */

    pthread_mutex_t floor_mutex; /* Protects active_word_count, carrier count   */
    pthread_cond_t  floor_cond;  /* Signals capacity/carrier changes            */
} Floor;


/* ============================================================
 * SECTION 8 – SHARED DATA  (SharedData)
 * ============================================================
 *
 * The root structure of the shared-memory region.  A single
 * instance of this struct is created by the parent with mmap()
 * using MAP_SHARED | MAP_ANONYMOUS, so every forked child
 * automatically inherits read/write access to it.
 *
 * All synchronisation primitives (mutexes, condition variables,
 * semaphores) inside this struct are initialised with the
 * PTHREAD_PROCESS_SHARED attribute so they work across process
 * boundaries (not just across threads).
 *
 * LOCK ORDERING (to avoid deadlocks):
 *   When two floor locks must be held simultaneously (word
 *   admission), always lock the lower-numbered floor first.
 *   All other mutexes are independent and must never be nested
 *   with floor locks.
 */
typedef struct {
    /* ── System configuration (read-only after init) ───────── */
    SystemConfig config;                          /* Command-line parameters     */

    /* ── Words ─────────────────────────────────────────────── */
    WordInfo words[MAX_WORDS];                    /* All words in the system     */
    int      total_words;                         /* Words read from input file  */
    int      completed_words;                     /* Words fully sorted so far   */

    /* ── Floors ─────────────────────────────────────────────── */
    Floor floors[MAX_FLOORS];                     /* One entry per floor         */

    /* ── Elevators ──────────────────────────────────────────── */
    ElevatorState delivery_elevator;              /* Transports characters       */
    ElevatorState reposition_elevator;            /* Relocates idle carriers     */

    /* ── Round-robin word selection ─────────────────────────── */
    int             round_robin_index;            /* Next word index to inspect  */
    pthread_mutex_t round_robin_mutex;            /* Atomises claim + index bump */

    /* ── Runtime statistics ─────────────────────────────────── */
    int total_retries;                            /* Words released without admit */
    int total_chars_transported;                  /* Characters delivered total  */
    int delivery_elevator_ops;                    /* Delivery drop-off count     */
    int reposition_elevator_ops;                  /* Reposition drop-off count   */
    int word_carrier_admissions[MAX_PROCESSES];   /* Admissions per word-carrier */
    int letter_carrier_transports[MAX_PROCESSES]; /* Deliveries per letter-carrier*/
    int sorting_process_completions[MAX_PROCESSES];/* Completions per sorter     */
    pthread_mutex_t stats_mutex;                  /* Protects all stat fields    */

    /* ── Global control ─────────────────────────────────────── */
    volatile int    system_running;    /* 0 → all processes should exit          */
    volatile int    all_words_admitted;/* 1 → word-carriers may exit             */
    pthread_mutex_t state_mutex;       /* Guards state_cond waits                */
    pthread_cond_t  state_cond;        /* Broadcast on any state change          */

    /* ── Process tracking ───────────────────────────────────── */
    pid_t           parent_pid;                   /* PID of the parent process   */
    pid_t           child_pids[MAX_PROCESSES];    /* PIDs of all child processes */
    int             num_children;                 /* Length of child_pids[]      */
    pthread_mutex_t children_mutex;               /* Protects child_pids[]       */
} SharedData;

#endif /* COMMON_H */
