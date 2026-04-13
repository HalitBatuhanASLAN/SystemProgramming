/*
 * Creates and destroys the single anonymous shared-memory region (SharedData) used by every process in the system.
 *
 * WHY mmap INSTEAD OF shmget/shm_open?
 * mmap(MAP_SHARED | MAP_ANONYMOUS) produces a region that:
 * 1. Requires no clean-up of named IPC objects.
 * 2. Is automatically inherited by fork()-ed children.
 * 3. Is visible to all descendants without any extra attachment step.
 * This makes it the simplest and most portable choice for a purely fork()-based system on Linux/POSIX.
 */

#include "shared_memory.h"

/*Steps performed:
 * 1. Allocate sizeof(SharedData) bytes with mmap().
 * 2. Zero the entire region so no field contains garbage.
 * 3. Copy the system configuration from the caller.
 * 4. Copy all pre-parsed words from the temporary stack array.
 * 5. Initialise per-floor metadata (ID, counts).
 * 6. Initialise both elevator states (floor, direction, load).
 * 7. Set global counters and control flags to their initial values.
*/
SharedData *shm_init(SystemConfig *config, WordInfo *words, int word_count)
{
    SharedData *data = mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if(data == MAP_FAILED)
    {
        /* mmap() sets errno on failure; include it in the message. */
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        return NULL;
    }

    /* ── Zero the entire region ─────────────────────────────── */
    /*
     * Even though MAP_ANONYMOUS guarantees zero pages from the OS,an explicit memset ensures correctness if this code were ever ported to a non-anonymous mapping and prevents subtle bugs from assumed zero-initialisation.
     */
    memset(data, 0, sizeof(SharedData));

    /* ── Copy system configuration ──────────────────────────── */
    /*
     * Every child process will read config fields directly from shared memory, so we store a full copy here rather than relying on the parent's local variable (which each child has its own copy of after fork, but that copy cannot be modified by other processes).
     */
    memcpy(&data->config, config, sizeof(SystemConfig));

    /* ── Copy word list ─────────────────────────────────────── */
    /*
     * temp_words[] in main() is a local (stack) array.  We must copy it into shared memory before forking so that all child processes see the same word data.
     */
    data->total_words     = word_count;
    data->completed_words = 0;
    memcpy(data->words, words, sizeof(WordInfo) * word_count);

    /* ── Initialise per-floor metadata ──────────────────────── */
    /*
     * Each floor starts with zero active words and its initial quota of letter-carriers (letter_carriers_per_floor).floor_mutex and floor_cond are initialised later by sync_init().
     */
    for(int i = 0;i < config->num_floors;i++)
    {
        data->floors[i].floor_id             = i;
        data->floors[i].active_word_count    = 0;
        data->floors[i].letter_carrier_count = config->letter_carriers_per_floor;
    }

    /* ── Initialise delivery elevator ───────────────────────── */
    /*
     * The delivery elevator starts at floor 0, idle, empty.Its capacity is the value supplied via -d on the command line.
     */
    data->delivery_elevator.current_floor = 0;
    data->delivery_elevator.direction     = DIR_IDLE;
    data->delivery_elevator.capacity      = config->delivery_elevator_capacity;
    data->delivery_elevator.current_load  = 0;
    data->delivery_elevator.queue_size     = 0;

    /* ── Initialise reposition elevator ─────────────────────── */
    /*
     * Identical starting state to the delivery elevator, but with
     * the reposition capacity (-r) and its own independent queue.
     */
    data->reposition_elevator.current_floor = 0;
    data->reposition_elevator.direction     = DIR_IDLE;
    data->reposition_elevator.capacity      = config->reposition_elevator_capacity;
    data->reposition_elevator.current_load  = 0;
    data->reposition_elevator.queue_size     = 0;

    /* ── Initialise global counters and control flags ────────── */
    data->round_robin_index        = 0; /* Start round-robin scan from word 0 */
    data->total_retries            = 0; /* No admission failures yet           */
    data->total_chars_transported  = 0; /* No characters moved yet             */
    data->delivery_elevator_ops    = 0; /* Delivery drop-off counter           */
    data->reposition_elevator_ops  = 0; /* Reposition drop-off counter         */
    data->system_running           = 1; /* 1 = run; 0 = all processes must exit*/
    data->all_words_admitted       = 0; /* Set to 1 once every word is admitted*/
    data->parent_pid               = getpid(); /* Stored for child reference   */
    data->num_children             = 0; /* No children registered yet          */

    return data;
}

/* Unmaps the shared-memory region.
 * Must be called AFTER:
 * 1. All child processes have exited (waitpid() collected).
 * 2. sync_destroy() has destroyed all synchronisation objects.
 * Calling munmap() while another process is still using the region would cause undefined behaviour (access to unmapped memory → SIGSEGV).*/
void shm_destroy(SharedData *data)
{
    if(data && data != MAP_FAILED)
    {
        if(munmap(data, sizeof(SharedData)) == -1)
            /* Non-fatal: report but do not abort. */
            fprintf(stderr, "Error: munmap failed: %s\n", strerror(errno));
    }
}