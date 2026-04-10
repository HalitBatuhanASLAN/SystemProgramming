/*
 * ============================================================
 * FILE: elevator_process.h
 * ------------------------------------------------------------
 * PURPOSE:
 *   Public interface for the elevator process module.
 *
 * THE TWO ELEVATORS:
 *
 *   Delivery Elevator
 *     Transports letter-carrier processes (each carrying one
 *     character) between floors.  Used whenever a character's
 *     destination floor differs from its source floor.
 *
 *   Reposition Elevator
 *     Relocates idle letter-carrier processes to random floors
 *     where new work might be waiting.  It does NOT carry
 *     characters; it only moves the carriers themselves.
 *
 * SHARED MOVEMENT POLICY (both elevators):
 *   1. Direction-based (UP / DOWN / IDLE).
 *   2. While moving in a direction, serve all requests in that
 *      direction before switching.
 *   3. Mandatory direction reversal at the top (floor N-1) and
 *      bottom (floor 0).
 *   4. Switch direction only when:
 *        (a) No further requests exist in the current direction,
 *        AND
 *        (b) The elevator is empty (current_load == 0).
 *   5. If IDLE and a request arrives, adopt the direction that
 *      leads to the requesting floor.
 *
 * CAPACITY:
 *   Each elevator has a maximum load (set via -d and -r).  Pick-up
 *   is skipped when current_load >= capacity; the request remains
 *   queued and will be served when space becomes available.
 * ============================================================
 */

#ifndef ELEVATOR_PROCESS_H
#define ELEVATOR_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * delivery_elevator_run – main loop for the delivery elevator process.
 *
 * Reads from data->delivery_elevator.queue[], picks up letter-
 * carriers (with characters) at their from_floor, and drops them
 * off at their to_floor.  Logs each pick-up and drop-off.
 *
 * Runs until data->system_running becomes 0.
 *
 * Parameters:
 *   data – pointer to the shared-memory region.
 */
void delivery_elevator_run(SharedData *data);

/*
 * reposition_elevator_run – main loop for the reposition elevator process.
 *
 * Reads from data->reposition_elevator.queue[], picks up idle
 * letter-carriers at their from_floor, and deposits them at a
 * random to_floor chosen by the requesting carrier.
 *
 * Follows the same movement policy as the delivery elevator but
 * operates on an entirely separate queue.
 *
 * Runs until data->system_running becomes 0.
 *
 * Parameters:
 *   data – pointer to the shared-memory region.
 */
void reposition_elevator_run(SharedData *data);

#endif /* ELEVATOR_PROCESS_H */
