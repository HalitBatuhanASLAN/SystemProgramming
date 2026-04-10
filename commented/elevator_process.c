/*
 * ============================================================
 * FILE: elevator_process.c
 * ------------------------------------------------------------
 * PURPOSE:
 *   Implements the movement logic for both elevators (delivery
 *   and reposition) in the multi-process word transportation
 *   system.
 *
 * MOVEMENT POLICY (identical for both elevators):
 *   ┌─────────────────────────────────────────────────────────┐
 *   │ 1. Start IDLE at floor 0.                               │
 *   │ 2. Block on sem_timedwait (50 ms) until a request       │
 *   │    arrives (sem_post by a carrier process).             │
 *   │ 3. Clean up served (served==1) requests from the queue. │
 *   │ 4. If queue is empty → remain IDLE and loop.            │
 *   │ 5. If IDLE → set direction toward the first request's   │
 *   │    from_floor.                                          │
 *   │ 6. At the current floor:                                │
 *   │    a. Drop off passengers whose to_floor matches.       │
 *   │    b. Pick up waiters whose from_floor matches          │
 *   │       (if capacity allows).                             │
 *   │ 7. Decide next floor:                                   │
 *   │    - Check whether any request lies further in the      │
 *   │      current direction.                                 │
 *   │    - If not, and elevator is empty: reconsider direction │
 *   │      based on remaining requests; or go IDLE.           │
 *   │    - Mandatory reversal at floor 0 and floor N-1.       │
 *   │ 8. Move one floor and log the movement.                 │
 *   └─────────────────────────────────────────────────────────┘
 *
 * SYNCHRONISATION:
 *   - All queue access is protected by elev_mutex.
 *   - After each drop-off or pick-up, elev_mutex is released
 *     briefly so log_msg() (which calls printf) does not hold
 *     the lock during I/O.
 *   - pthread_cond_broadcast(elev_cond) is called after every
 *     state change so waiting carrier processes (polling their
 *     served flag) wake up promptly.
 * ============================================================
 */

#include "elevator_process.h"
#include "utils.h"

/* ============================================================
 * clean_served_requests  (file-scope helper)
 * ------------------------------------------------------------
 * Removes all requests with served == 1 from the elevator's
 * queue by compacting the array in-place (shifting elements
 * with served != 1 toward index 0).
 *
 * This is called at the start of each elevator cycle so the
 * queue only ever contains pending or in-transit requests.
 *
 * Must be called with elev_mutex held.
 * ============================================================ */
static void clean_served_requests(ElevatorState *elev) {
    int write_idx = 0;
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served != 1) {
            /* Keep this request; shift it down if needed. */
            if (write_idx != i) {
                elev->queue[write_idx] = elev->queue[i];
            }
            write_idx++;
        }
        /* Requests with served == 1 are simply skipped (deleted). */
    }
    elev->queue_size = write_idx;
}

/* ============================================================
 * has_any_pending  (file-scope helper)
 * ------------------------------------------------------------
 * Returns 1 if the queue contains at least one request that has
 * not yet been fully delivered (served == 0 or served == 2).
 * Returns 0 if the queue is empty or every entry is served.
 *
 * served values:
 *   0 = waiting to be picked up
 *   2 = currently inside the elevator (in transit)
 *   1 = delivered (will be removed by clean_served_requests)
 *
 * Must be called with elev_mutex held.
 * ============================================================ */
static int has_any_pending(ElevatorState *elev) {
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served == 0 || elev->queue[i].served == 2)
            return 1;
    }
    return 0;
}

/* ============================================================
 * determine_direction  (file-scope helper)
 * ------------------------------------------------------------
 * Inspects the first unclaimed (served == 0) request and
 * returns the direction the elevator should take to reach it
 * from the current floor:
 *   DIR_UP   – the request's from_floor is above current.
 *   DIR_DOWN – the request's from_floor is below current.
 *   DIR_IDLE – request is on the current floor; use to_floor
 *              to decide (character needs to go up or down).
 *
 * Must be called with elev_mutex held.
 * ============================================================ */
static int determine_direction(ElevatorState *elev) {
    for (int i = 0; i < elev->queue_size; i++) {
        if (elev->queue[i].served == 0) {
            /* The requester is above → go up. */
            if (elev->queue[i].from_floor > elev->current_floor)
                return DIR_UP;
            /* The requester is below → go down. */
            if (elev->queue[i].from_floor < elev->current_floor)
                return DIR_DOWN;
            /*
             * The elevator is already at the requester's floor.
             * Use the destination to determine travel direction.
             */
            if (elev->queue[i].to_floor > elev->current_floor)
                return DIR_UP;
            if (elev->queue[i].to_floor < elev->current_floor)
                return DIR_DOWN;
        }
    }
    return DIR_IDLE; /* No pending request found. */
}


/* ============================================================
 *  DELIVERY ELEVATOR
 * ============================================================ */

void delivery_elevator_run(SharedData *data) {
    ElevatorState *elev      = &data->delivery_elevator;
    int            num_floors = data->config.num_floors;

    while (data->system_running) {

        /* ── Wait for a request (non-busy-waiting) ─────────── */
        /*
         * sem_timedwait blocks until a carrier calls sem_post()
         * after enqueuing a request, OR until the 50 ms timeout
         * expires (whichever comes first).  The timeout prevents
         * the elevator from freezing if a sem_post is missed.
         */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000; /* 50 ms */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        sem_timedwait(&elev->request_sem, &ts);

        if (!data->system_running) break;

        pthread_mutex_lock(&elev->elev_mutex);

        /* Remove completed requests before processing new ones. */
        clean_served_requests(elev);

        /* Nothing to do → reset direction and sleep again. */
        if (!has_any_pending(elev)) {
            elev->direction = DIR_IDLE;
            pthread_mutex_unlock(&elev->elev_mutex);
            continue;
        }

        /*
         * If the elevator was idle, choose a direction based on
         * the oldest pending request's location.
         */
        if (elev->direction == DIR_IDLE) {
            elev->direction = determine_direction(elev);
            if (elev->direction == DIR_IDLE) {
                pthread_mutex_unlock(&elev->elev_mutex);
                continue;
            }
        }

        /* ── DROP OFF: unload passengers at the current floor ── */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 2 &&
                elev->queue[i].to_floor == elev->current_floor) {

                /* Mark as delivered. */
                elev->queue[i].served = 1;
                elev->current_load--;

                /*
                 * Release the lock before calling log_msg() to avoid
                 * holding it during printf (which can be slow and would
                 * block any carrier trying to enqueue a new request).
                 */
                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Delivery elevator drop off at floor %d "
                        "(currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d carrying char '%c' of word %d",
                        elev->current_floor, elev->current_load,
                        elev->queue[i].carrier_id,
                        elev->queue[i].character,
                        elev->queue[i].word_id);

                /* Update the delivery operations counter. */
                pthread_mutex_lock(&data->stats_mutex);
                data->delivery_elevator_ops++;
                pthread_mutex_unlock(&data->stats_mutex);

                /*
                 * Broadcast on elev_cond: the waiting carrier process
                 * is polling for served==1 on elev_cond; wake it now.
                 */
                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* ── PICK UP: board passengers at the current floor ──── */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 0 &&
                elev->queue[i].from_floor == elev->current_floor &&
                elev->current_load < elev->capacity) {

                /* Mark as in transit inside the elevator. */
                elev->queue[i].served = 2;
                elev->current_load++;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Delivery elevator pick up "
                        "(currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d carrying char '%c' of word %d",
                        elev->current_load,
                        elev->queue[i].carrier_id,
                        elev->queue[i].character,
                        elev->queue[i].word_id);

                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* ── Decide whether to continue in the current direction */
        /*
         * Search the queue for any request (waiting or in transit)
         * that lies beyond the current floor in our travel direction.
         * If found, we must continue; otherwise we may switch.
         */
        int should_continue = 0;
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 1) continue;

            /*
             * For a waiting request the relevant floor is from_floor
             * (where we need to pick them up).
             * For an in-transit request it is to_floor (where we
             * need to drop them off).
             */
            int relevant_floor = -1;
            if (elev->queue[i].served == 0)
                relevant_floor = elev->queue[i].from_floor;
            else if (elev->queue[i].served == 2)
                relevant_floor = elev->queue[i].to_floor;

            if (relevant_floor < 0) continue;

            if (elev->direction == DIR_UP   && relevant_floor > elev->current_floor) {
                should_continue = 1;
                break;
            }
            if (elev->direction == DIR_DOWN && relevant_floor < elev->current_floor) {
                should_continue = 1;
                break;
            }
        }

        /*
         * If no request lies further in the current direction AND
         * the elevator is empty, reconsider direction.
         */
        if (!should_continue && elev->current_load == 0) {
            if (has_any_pending(elev)) {
                /* There are requests in the opposite direction. */
                elev->direction = determine_direction(elev);
            } else {
                elev->direction = DIR_IDLE;
            }
        }

        /* ── Mandatory reversal at building boundaries ─────── */
        /*
         * The elevator must NEVER move past floor 0 or floor N-1.
         * If we are at the top going UP, switch to DOWN; vice versa.
         */
        if (elev->current_floor >= num_floors - 1 &&
            elev->direction == DIR_UP) {
            elev->direction = DIR_DOWN;
        } else if (elev->current_floor <= 0 &&
                   elev->direction == DIR_DOWN) {
            elev->direction = DIR_UP;
        }

        /* ── Move one floor in the current direction ────────── */
        if (elev->direction == DIR_UP &&
            elev->current_floor < num_floors - 1) {

            elev->current_floor++;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Delivery elevator moving UP");
            log_msg("Delivery elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);

        } else if (elev->direction == DIR_DOWN &&
                   elev->current_floor > 0) {

            elev->current_floor--;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Delivery elevator moving DOWN");
            log_msg("Delivery elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        }

        pthread_mutex_unlock(&elev->elev_mutex);
    }
}


/* ============================================================
 *  REPOSITION ELEVATOR
 * ============================================================
 *
 * Identical movement policy to the delivery elevator; the only
 * differences are:
 *   - It operates on data->reposition_elevator (separate queue).
 *   - It carries letter-CARRIERS (not characters), so log messages
 *     show carrier_id only (no word_id / character fields).
 *   - It updates data->reposition_elevator_ops instead of
 *     delivery_elevator_ops.
 * ============================================================ */

void reposition_elevator_run(SharedData *data) {
    ElevatorState *elev      = &data->reposition_elevator;
    int            num_floors = data->config.num_floors;

    while (data->system_running) {

        /* ── Wait for a reposition request ──────────────────── */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000; /* 50 ms */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        sem_timedwait(&elev->request_sem, &ts);

        if (!data->system_running) break;

        pthread_mutex_lock(&elev->elev_mutex);

        clean_served_requests(elev);

        if (!has_any_pending(elev)) {
            elev->direction = DIR_IDLE;
            pthread_mutex_unlock(&elev->elev_mutex);
            continue;
        }

        if (elev->direction == DIR_IDLE) {
            elev->direction = determine_direction(elev);
            if (elev->direction == DIR_IDLE) {
                pthread_mutex_unlock(&elev->elev_mutex);
                continue;
            }
        }

        /* ── DROP OFF idle carriers at their destination floor ─ */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 2 &&
                elev->queue[i].to_floor == elev->current_floor) {

                elev->queue[i].served = 1;
                elev->current_load--;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Reposition elevator drop off at floor %d "
                        "(currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d",
                        elev->current_floor, elev->current_load,
                        elev->queue[i].carrier_id);

                pthread_mutex_lock(&data->stats_mutex);
                data->reposition_elevator_ops++;
                pthread_mutex_unlock(&data->stats_mutex);

                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* ── PICK UP idle carriers at the current floor ──────── */
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 0 &&
                elev->queue[i].from_floor == elev->current_floor &&
                elev->current_load < elev->capacity) {

                elev->queue[i].served = 2;
                elev->current_load++;

                pthread_mutex_unlock(&elev->elev_mutex);
                log_msg("Reposition elevator pick up "
                        "(currently %d letter-carrier inside):\n"
                        "  Letter-carrier-process_%d",
                        elev->current_load,
                        elev->queue[i].carrier_id);

                pthread_cond_broadcast(&elev->elev_cond);
                pthread_mutex_lock(&elev->elev_mutex);
            }
        }

        /* ── Direction maintenance (same logic as delivery) ──── */
        int should_continue = 0;
        for (int i = 0; i < elev->queue_size; i++) {
            if (elev->queue[i].served == 1) continue;

            int relevant_floor = -1;
            if (elev->queue[i].served == 0)
                relevant_floor = elev->queue[i].from_floor;
            else if (elev->queue[i].served == 2)
                relevant_floor = elev->queue[i].to_floor;

            if (relevant_floor < 0) continue;

            if (elev->direction == DIR_UP   && relevant_floor > elev->current_floor) {
                should_continue = 1;
                break;
            }
            if (elev->direction == DIR_DOWN && relevant_floor < elev->current_floor) {
                should_continue = 1;
                break;
            }
        }

        if (!should_continue && elev->current_load == 0) {
            if (has_any_pending(elev))
                elev->direction = determine_direction(elev);
            else
                elev->direction = DIR_IDLE;
        }

        /* Mandatory reversal at building boundaries. */
        if (elev->current_floor >= num_floors - 1 &&
            elev->direction == DIR_UP) {
            elev->direction = DIR_DOWN;
        } else if (elev->current_floor <= 0 &&
                   elev->direction == DIR_DOWN) {
            elev->direction = DIR_UP;
        }

        /* ── Move one floor ─────────────────────────────────── */
        if (elev->direction == DIR_UP &&
            elev->current_floor < num_floors - 1) {

            elev->current_floor++;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Reposition elevator moving UP");
            log_msg("Reposition elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);

        } else if (elev->direction == DIR_DOWN &&
                   elev->current_floor > 0) {

            elev->current_floor--;
            pthread_mutex_unlock(&elev->elev_mutex);
            log_msg("Reposition elevator moving DOWN");
            log_msg("Reposition elevator arrived at floor %d", elev->current_floor);
            pthread_mutex_lock(&elev->elev_mutex);
        }

        pthread_mutex_unlock(&elev->elev_mutex);
    }
}
