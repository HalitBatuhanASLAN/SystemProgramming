#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include "common.h"

/* Shared memory olusturur ve initialize eder.
 * mmap ile MAP_SHARED | MAP_ANONYMOUS kullanir.
 * Basarili: SharedData pointer, Hata: NULL */
SharedData* shm_init(SystemConfig *config, WordInfo *words, int word_count);

/* Shared memory'yi temizler ve munmap yapar */
void shm_destroy(SharedData *data);

#endif /* SHARED_MEMORY_H */