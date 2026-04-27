#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include "shm.h"

typedef struct {
    shm_region_c_t*  region_c;
    shm_region_d_t*  region_d;
    char**           keywords;
    int              n_keywords;
    int              n_files;
    int              timeout_sec;
    const char*      output_path;
    const char*      binary_path;
    const char*      filter_path;
} aggregator_arg_t;

void aggregator_process_main(aggregator_arg_t* arg);

#endif /* AGGREGATOR_H */
