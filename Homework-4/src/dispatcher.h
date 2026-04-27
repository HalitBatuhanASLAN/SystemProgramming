#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "shm.h"

typedef struct {
    shm_region_a_t*  region_a;
    shm_region_b_t*  region_b[LEVEL_COUNT];
    shm_region_c_t*  region_c;
    shm_region_d_t*  region_d;
    char**           priority_sources;
    int              n_priority_sources;
    int              timeout_sec;
    int              n_readers;
} dispatcher_arg_t;

void dispatcher_process_main(dispatcher_arg_t* arg);

#endif /* DISPATCHER_H */
