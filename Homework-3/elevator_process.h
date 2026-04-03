#ifndef ELEVATOR_PROCESS_H
#define ELEVATOR_PROCESS_H

#include "common.h"

/* Delivery elevator process ana fonksiyonu.
 * Harfleri katlar arasi tasir. */
void delivery_elevator_run(SharedData *data);

/* Reposition elevator process ana fonksiyonu.
 * Bos letter-carrier'lari rastgele katlara tasir. */
void reposition_elevator_run(SharedData *data);

#endif /* ELEVATOR_PROCESS_H */