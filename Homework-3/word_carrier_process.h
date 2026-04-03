#ifndef WORD_CARRIER_PROCESS_H
#define WORD_CARRIER_PROCESS_H

#include "common.h"

/* Word-carrier process ana fonksiyonu.
 * floor_id: bu carrier'in calistigi kat
 * carrier_id: bu carrier'in benzersiz ID'si (loglama icin) */
void word_carrier_run(SharedData *data, int floor_id, int carrier_id);

#endif /* WORD_CARRIER_PROCESS_H */