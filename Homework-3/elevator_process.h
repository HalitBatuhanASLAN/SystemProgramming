/* PURPOSE:
 * Public interface for the elevator process module.
 *
 * THE TWO ELEVATORS:
 *
 * Delivery Elevator
 * Transports letter-carrier processes (each carrying one character) between floors.  Used whenever a character's destination floor differs from its source floor.
 *
 * Reposition Elevator
 * Relocates idle letter-carrier processes to random floors where new work might be waiting.  It does NOT carry characters; it only moves the carriers themselves.
 *
 */

#ifndef ELEVATOR_PROCESS_H
#define ELEVATOR_PROCESS_H

#include "common.h"   /* SharedData */

/*
 * Reads from data->delivery_elevator.queue[], picks up letter-carriers (with characters) at their from_floor, and drops them off at their to_floor.  Logs each pick-up and drop-off.
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
 */
void reposition_elevator_run(SharedData *data);

#endif