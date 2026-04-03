#ifndef SORTING_PROCESS_H
#define SORTING_PROCESS_H

#include "common.h"

/* Sorting process ana fonksiyonu.
 * floor_id: bu sorter'in calistigi kat (hic degismez)
 * sorter_id: bu sorter'in benzersiz ID'si */
void sorting_process_run(SharedData *data, int floor_id, int sorter_id);

#endif /* SORTING_PROCESS_H */