#ifndef LETTER_CARRIER_PROCESS_H
#define LETTER_CARRIER_PROCESS_H

#include "common.h"

/* Letter-carrier process ana fonksiyonu.
 * initial_floor: baslangic kati
 * carrier_id: bu carrier'in benzersiz ID'si */
void letter_carrier_run(SharedData *data, int initial_floor, int carrier_id);

#endif /* LETTER_CARRIER_PROCESS_H */