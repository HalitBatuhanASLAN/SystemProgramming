#include "shared_memory.h"

SharedData* shm_init(SystemConfig *config, WordInfo *words, int word_count) {
    /* mmap ile anonymous shared memory olustur */
    SharedData *data = mmap(NULL, sizeof(SharedData),
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed: %s\n", strerror(errno));
        return NULL;
    }

    /* Tum alani sifirla */
    memset(data, 0, sizeof(SharedData));

    /* Konfigurasyonu kopyala */
    memcpy(&data->config, config, sizeof(SystemConfig));

    /* Kelimeleri kopyala */
    data->total_words = word_count;
    data->completed_words = 0;
    memcpy(data->words, words, sizeof(WordInfo) * word_count);

    /* Katlari initialize et */
    for (int i = 0; i < config->num_floors; i++) {
        data->floors[i].floor_id = i;
        data->floors[i].active_word_count = 0;
        data->floors[i].letter_carrier_count = config->letter_carriers_per_floor;
    }

    /* Delivery elevator init */
    data->delivery_elevator.current_floor = 0;
    data->delivery_elevator.direction = DIR_IDLE;
    data->delivery_elevator.capacity = config->delivery_elevator_capacity;
    data->delivery_elevator.current_load = 0;
    data->delivery_elevator.queue_size = 0;

    /* Reposition elevator init */
    data->reposition_elevator.current_floor = 0;
    data->reposition_elevator.direction = DIR_IDLE;
    data->reposition_elevator.capacity = config->reposition_elevator_capacity;
    data->reposition_elevator.current_load = 0;
    data->reposition_elevator.queue_size = 0;

    /* Global durum */
    data->round_robin_index = 0;
    data->total_retries = 0;
    data->total_chars_transported = 0;
    data->delivery_elevator_ops = 0;
    data->reposition_elevator_ops = 0;
    data->system_running = 1;
    data->all_words_admitted = 0;
    data->parent_pid = getpid();
    data->num_children = 0;

    return data;
}

void shm_destroy(SharedData *data) {
    if (data && data != MAP_FAILED) {
        if (munmap(data, sizeof(SharedData)) == -1) {
            fprintf(stderr, "Error: munmap failed: %s\n", strerror(errno));
        }
    }
}