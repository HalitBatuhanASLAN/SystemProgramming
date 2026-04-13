/*
 * PURPOSE:
 * Implements the letter-carrier process main loop and all
 * helper functions it depends on:
 * - request_delivery_elevator()   – enqueue a delivery request.
 * - request_reposition_elevator() – enqueue a reposition request.
 * - deliver_char_to_sorting_area()– place a character in a word's
 * sorting area on the dest floor.
 * - find_task_on_floor()           – find an unclaimed CharTask
 * on the current floor.
 * - letter_carrier_run()           – the process main loop.
 *
 * CONCURRENCY NOTES:
 * - CharTask.claimed is set to 1 while holding word->word_mutex,so two carriers can never claim the same task.
 * - The elevator request queue is protected by elev_mutex.
 * - After enqueuing a request the carrier releases the mutex and waits on elev_cond (with a 50 ms timeout) until the elevator sets served==1 or removes the entry from the queue.
 * - floor letter_carrier_count is updated under floor_mutex to keep the parent's monitoring accurate.
 */

#include "letter_carrier_process.h"
#include "utils.h"

/* 
 * Adds a new entry to the delivery elevator's request queue so that the elevator process will pick up this letter-carrier (carrying one character) at from_floor and drop it off at to_floor.
 *
 * After enqueuing:
 * 1. sem_post() wakes the elevator process (which sleeps on sem_timedwait when the queue is empty).
 * 2. pthread_cond_broadcast() wakes any other waiters on the elevator's condition variable.*/
static void request_delivery_elevator(SharedData *data,int from_floor, int to_floor,int carrier_id, int word_id,char character, int original_index)
{
    ElevatorState *elev = &data->delivery_elevator;

    pthread_mutex_lock(&elev->elev_mutex);

    /* Only add the request if the queue is not full. */
    if(elev->queue_size < MAX_QUEUE_SIZE)
    {
        ElevatorRequest *req = &elev->queue[elev->queue_size];
        req->requester_type  = 0;               /* 0 = delivery request       */
        req->from_floor      = from_floor;
        req->to_floor        = to_floor;
        req->carrier_id      = carrier_id;
        req->word_id         = word_id;
        req->character       = character;
        req->original_index  = original_index;
        req->served          = 0;               /* 0 = waiting to be picked up */
        elev->queue_size++;
    }

    pthread_mutex_unlock(&elev->elev_mutex);

    /* Wake the elevator process so it sees the new request. */
    sem_post(&elev->request_sem);

    pthread_mutex_lock(&elev->elev_mutex);
    pthread_cond_broadcast(&elev->elev_cond);
    pthread_mutex_unlock(&elev->elev_mutex);
}

/* 
 * Adds a new entry to the REPOSITION elevator's queue so that an idle letter-carrier is moved to a random different floor.
 *
 * The target floor is chosen randomly here (not by the elevator) so the carrier already knows its destination and can update current_floor as soon as the ride completes.
*/
static void request_reposition_elevator(SharedData *data,int from_floor, int carrier_id,int *out_to_floor)
{
    ElevatorState *elev = &data->reposition_elevator;
    int to_floor;

    /*
     * Pick a random target floor that is different from the current floor.  The loop prevents staying in place when there is more than one floor available.
     */
    do
    {
        to_floor = rand_range(data->config.num_floors);
    } while(to_floor == from_floor && data->config.num_floors > 1);

    *out_to_floor = to_floor;

    pthread_mutex_lock(&elev->elev_mutex);

    if(elev->queue_size < MAX_QUEUE_SIZE)
    {
        ElevatorRequest *req = &elev->queue[elev->queue_size];
        req->requester_type  = 1;   /* 1 = reposition request              */
        req->from_floor      = from_floor;
        req->to_floor        = to_floor;
        req->carrier_id      = carrier_id;
        req->word_id         = -1;  /* No word being carried                */
        req->character       = '\0';
        req->original_index  = -1;
        req->served          = 0;
        elev->queue_size++;
    }

    pthread_mutex_unlock(&elev->elev_mutex);

    sem_post(&elev->request_sem);

    pthread_mutex_lock(&elev->elev_mutex);
    pthread_cond_broadcast(&elev->elev_cond);
    pthread_mutex_unlock(&elev->elev_mutex);
}

/* 
 * Places the transported character into the first available (non-occupied, non-fixed) slot of the word's sorting_area[] on the sorting floor.
 *
 * Assignment rule: characters are NOT placed directly into their final correct index.  They go into the FIRST empty slot; sorting processes then rearrange them.
 *
 * After placement, the function:
 * - Updates the total_chars_transported statistic.
 * - Broadcasts on floor_cond so sorting processes wake up.
 * - Broadcasts on state_cond for general state-change listeners.*/
static void deliver_char_to_sorting_area(SharedData *data,int word_idx, char character,int original_index, int carrier_id)
{
    (void)original_index;  /* Kept in signature for future use / logging. */
    WordInfo *word = &data->words[word_idx];

    pthread_mutex_lock(&word->word_mutex);

    /* Find the first slot that is not yet occupied and not fixed. */
    for(int i = 0;i < word->word_len;i++)
    {
        if(!word->occupied[i] && !word->fixed[i])
        {
            word->sorting_area[i] = character;
            word->occupied[i]     = 1;

            pthread_mutex_unlock(&word->word_mutex);

            log_msg("Letter-carrier-process_%d brought char '%c' of word %d to floor %d",
                    carrier_id, character, word->word_id, word->sorting_floor);

            /* Update statistics (protected by stats_mutex). */
            pthread_mutex_lock(&data->stats_mutex);
            data->total_chars_transported++;
            data->letter_carrier_transports[carrier_id]++;
            pthread_mutex_unlock(&data->stats_mutex);

            /* Wake sorting processes on this floor. */
            pthread_mutex_lock(&data->floors[word->sorting_floor].floor_mutex);
            pthread_cond_broadcast(&data->floors[word->sorting_floor].floor_cond);
            pthread_mutex_unlock(&data->floors[word->sorting_floor].floor_mutex);

            /* Wake any general state watchers (parent monitor, etc.). */
            pthread_mutex_lock(&data->state_mutex);
            pthread_cond_broadcast(&data->state_cond);
            pthread_mutex_unlock(&data->state_mutex);

            return;
        }
    }

    /*
     * If no free slot was found, the character is silently dropped.
     * This should not happen if the system is working correctly (each word starts with all slots empty and chars arrive one at a time), but releasing the mutex is still mandatory.
     */
    pthread_mutex_unlock(&word->word_mutex);
}

/*
 * Searches for an unclaimed, undelivered CharTask among all admitted, incomplete words whose arrival_floor matches current_floor.
 *
 * Search order is randomised (random starting word, random starting character) to reduce contention when multiple letter-carriers are on the same floor.
 *
 * If a task is found, its claimed flag is set to 1 while holding word->word_mutex, and the caller is given the word index and char task index via output parameters.
*/
static int find_task_on_floor(SharedData *data, int current_floor,int *out_word_idx, int *out_char_idx)
{
    /* Start from a random word to distribute load. */
    int start_word = rand_range(data->total_words);

    for(int attempt = 0;attempt < data->total_words;attempt++)
    {
        int i = (start_word + attempt) % data->total_words;
        WordInfo *w = &data->words[i];

        /* Skip words not yet in the system, already done, or on a different floor. */
        if(!w->admitted || w->completed)
            continue;
        if(w->arrival_floor != current_floor)
            continue;

        pthread_mutex_lock(&w->word_mutex);

        /* Randomise character scan order within this word. */
        int start_char = rand_range(w->num_char_tasks);
        for(int jj = 0;jj < w->num_char_tasks;jj++)
        {
            int j = (start_char + jj) % w->num_char_tasks;

            if(!w->char_tasks[j].claimed && !w->char_tasks[j].delivered)
            {
                /* Atomically claim this task before releasing the lock. */
                w->char_tasks[j].claimed = 1;
                *out_word_idx = i;
                *out_char_idx = j;
                pthread_mutex_unlock(&w->word_mutex);
                return 1; /* Found and claimed. */
            }
        }

        pthread_mutex_unlock(&w->word_mutex);
    }

    return 0; /* No available task on this floor. */
}

/* Main loop of a letter-carrier process.
 *
 * The loop alternates between two modes:
 * WORK MODE   – a CharTask was found; deliver it (same floor or via delivery elevator to a different floor).
 * IDLE MODE   – no task available; use reposition elevator to move to a random floor with (hopefully) more work.
*/
void letter_carrier_run(SharedData *data, int initial_floor, int carrier_id)
{
    int current_floor = initial_floor;

    while(data->system_running)
    {
        int word_idx, char_idx;

        /* ── Try to find work on the current floor ───────────── */
        if(find_task_on_floor(data, current_floor, &word_idx, &char_idx))
        {
            WordInfo *word = &data->words[word_idx];
            CharTask *task = &word->char_tasks[char_idx];

            log_msg("Letter-carrier-process_%d selected char '%c' of word %d from floor %d",
                    carrier_id, task->character, word->word_id, current_floor);

            /* ── CASE A: Same-floor delivery ─────────────────── */
            if(task->dest_floor == current_floor)
            {
                /*
                 * The character's destination is the current floor;no elevator is needed.  Place it directly.
                 */
                log_msg("Destination is same floor -> direct placement");

                deliver_char_to_sorting_area(data, word_idx,task->character,task->original_index,carrier_id);

                /* Mark the task as delivered. */
                pthread_mutex_lock(&word->word_mutex);
                task->delivered = 1;
                pthread_mutex_unlock(&word->word_mutex);

            }
            else
            {
                /* ── CASE B: Cross-floor delivery via elevator ── */
                log_msg("Letter-carrier-process_%d requested delivery elevator "
                        "from floor %d to floor %d",
                        carrier_id, current_floor, task->dest_floor);

                /*
                 * Cache the task fields before releasing word_mutex so we can safely reference them while waiting for the elevator (the pointer might become stale if the word were somehow relocated, though that does not happen in this implementation).
                 */
                int  dest = task->dest_floor;
                int  widx = word->word_id;
                int  oidx = task->original_index;
                char ch   = task->character;

                /* Enqueue a delivery request. */
                request_delivery_elevator(data, current_floor, dest,
                                           carrier_id, widx, ch, oidx);

                /*
                 * Wait until the elevator has dropped us off (served == 1)
                 * OR until the request has been purged from the queue
                 * (which also means delivery is complete).
                 *
                 * We use a 50 ms timed wait to avoid permanent blocking
                 * if a broadcast is missed.
                 */
                ElevatorState *elev = &data->delivery_elevator;
                int arrived = 0;

                while(!arrived && data->system_running)
                {
                    pthread_mutex_lock(&elev->elev_mutex);

                    /* Check if our request has served == 1. */
                    for(int k = 0;k < elev->queue_size;k++)
                    {
                        if(elev->queue[k].carrier_id    == carrier_id &&
                            elev->queue[k].word_id       == widx       &&
                            elev->queue[k].original_index == oidx       &&
                            elev->queue[k].served == 1)
                        {
                            arrived = 1;
                            break;
                        }
                    }

                    /*
                     * The elevator's clean_served_requests() may have already removed our entry (served==1 entries are deleted after processing).  If the request is no longer in the queue at all, delivery is done.
                     */
                    if(!arrived && data->system_running)
                    {
                        int still_in_queue = 0;
                        for(int k = 0;k < elev->queue_size;k++)
                        {
                            if(elev->queue[k].carrier_id    == carrier_id &&elev->queue[k].word_id == widx && elev->queue[k].original_index == oidx)
                            {
                                still_in_queue = 1;
                                break;
                            }
                        }
                        if(!still_in_queue)
                            arrived = 1; /* Request gone = delivered. */
                    }

                    /* Sleep until next elevator event (or timeout). */
                    if(!arrived && data->system_running)
                    {
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_nsec += 50000000; /* 50 ms */
                        if(ts.tv_nsec >= 1000000000)
                        {
                            ts.tv_sec++;
                            ts.tv_nsec -= 1000000000;
                        }
                        pthread_cond_timedwait(&elev->elev_cond,&elev->elev_mutex, &ts);
                    }

                    pthread_mutex_unlock(&elev->elev_mutex);
                }

                /* ── After the ride: update state ────────────── */
                if(arrived)
                {
                    /*
                     * The carrier is now on the destination floor.Update the letter_carrier_count on both floors so the parent's monitoring stays accurate.
                     */
                    if(current_floor != dest)
                    {
                        pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                        data->floors[current_floor].letter_carrier_count--;
                        pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);

                        pthread_mutex_lock(&data->floors[dest].floor_mutex);
                        data->floors[dest].letter_carrier_count++;
                        pthread_mutex_unlock(&data->floors[dest].floor_mutex);
                    }
                    current_floor = dest;

                    /* Place the character in the sorting area. */
                    deliver_char_to_sorting_area(data, word_idx,ch, oidx, carrier_id);

                    /* Mark the task as delivered. */
                    pthread_mutex_lock(&word->word_mutex);
                    task->delivered = 1;
                    pthread_mutex_unlock(&word->word_mutex);
                }
            }

        }
        else
        {
            /* ── IDLE MODE: no work on this floor ───────────── */
            log_msg("Letter-carrier-process_%d found no available task on floor %d",carrier_id, current_floor);

            if(data->config.num_floors > 1)
            {
                /* Multi-floor building: reposition to a random floor. */
                log_msg("Letter-carrier-process_%d requested reposition elevator ""from floor %d", carrier_id, current_floor);

                int to_floor = current_floor;
                request_reposition_elevator(data, current_floor,carrier_id, &to_floor);

                ElevatorState *elev = &data->reposition_elevator;
                int arrived   = 0;
                int new_floor = to_floor;

                while(!arrived && data->system_running)
                {
                    pthread_mutex_lock(&elev->elev_mutex);

                    /* Check served == 1 for our specific request. */
                    for(int k = 0;k < elev->queue_size;k++)
                    {
                        if(elev->queue[k].carrier_id == carrier_id &&
                            elev->queue[k].to_floor   == to_floor   &&
                            elev->queue[k].served == 1)
                        {
                            arrived = 1;
                            break;
                        }
                    }

                    /* Request may have been cleaned up already. */
                    if(!arrived && data->system_running)
                    {
                        int still_in_queue = 0;
                        for(int k = 0;k < elev->queue_size;k++)
                        {
                            if(elev->queue[k].carrier_id == carrier_id &&
                                elev->queue[k].to_floor   == to_floor)
                            {
                                still_in_queue = 1;
                                break;
                            }
                        }
                        if(!still_in_queue)
                            arrived = 1;
                    }

                    if(!arrived && data->system_running)
                    {
                        struct timespec ts;
                        clock_gettime(CLOCK_REALTIME, &ts);
                        ts.tv_nsec += 50000000; /* 50 ms */
                        if(ts.tv_nsec >= 1000000000)
                        {
                            ts.tv_sec++;
                            ts.tv_nsec -= 1000000000;
                        }
                        pthread_cond_timedwait(&elev->elev_cond,&elev->elev_mutex, &ts);
                    }

                    pthread_mutex_unlock(&elev->elev_mutex);
                }

                /* ── After the reposition ride ───────────────── */
                if(arrived)
                {
                    int old_floor = current_floor;
                    current_floor = new_floor;

                    /* Update letter_carrier_count on both floors. */
                    if(old_floor != current_floor)
                    {
                        pthread_mutex_lock(&data->floors[old_floor].floor_mutex);
                        data->floors[old_floor].letter_carrier_count--;
                        pthread_cond_broadcast(&data->floors[old_floor].floor_cond);
                        pthread_mutex_unlock(&data->floors[old_floor].floor_mutex);

                        pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                        data->floors[current_floor].letter_carrier_count++;
                        pthread_cond_broadcast(&data->floors[current_floor].floor_cond);
                        pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);
                    }

                    /* Wake anything waiting on a general state change. */
                    pthread_mutex_lock(&data->state_mutex);
                    pthread_cond_broadcast(&data->state_cond);
                    pthread_mutex_unlock(&data->state_mutex);

                    log_msg("Letter-carrier-process_%d resumed work on floor %d",carrier_id, current_floor);
                }

            }
            else
            {
                /* Single-floor building: there is nowhere to reposition.*/
                pthread_mutex_lock(&data->floors[current_floor].floor_mutex);
                if(data->system_running)
                {
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    ts.tv_nsec += 100000000; /* 100 ms */
                    if(ts.tv_nsec >= 1000000000)
                    {
                        ts.tv_sec++;
                        ts.tv_nsec -= 1000000000;
                    }
                    pthread_cond_timedwait(&data->floors[current_floor].floor_cond,&data->floors[current_floor].floor_mutex, &ts);
                }
                pthread_mutex_unlock(&data->floors[current_floor].floor_mutex);
            }
        }
    }
}