#ifndef ANALYZER_H
#define ANALYZER_H

#include "shm.h"

typedef struct {
    int              level_idx;      /* 0=ERROR..3=DEBUG */
    int              n_workers;      /* W */
    shm_region_b_t*  region_b;
    shm_region_c_t*  region_c;
    char**           keywords;
    int              n_keywords;
} analyzer_arg_t;

/* Overlapping substring sayımı */
int count_overlapping(const char* text, const char* keyword);

void analyzer_process_main(analyzer_arg_t* arg);

#endif /* ANALYZER_H */
