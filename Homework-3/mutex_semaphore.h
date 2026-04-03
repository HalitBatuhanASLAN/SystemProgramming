#ifndef MUTEX_SEMAPHORE_H
#define MUTEX_SEMAPHORE_H

#include "common.h"

/* Tum senkronizasyon primitive'lerini initialize eder.
 * Per-word mutex, floor mutex/cond, elevator mutex/cond/sem, global mutex'ler.
 * Basarili: 0, Hata: -1 */
int sync_init(SharedData *data);

/* Tum senkronizasyon primitive'lerini yok eder */
void sync_destroy(SharedData *data);

#endif /* MUTEX_SEMAPHORE_H */