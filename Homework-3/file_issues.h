#ifndef FILE_ISSUES_H
#define FILE_ISSUES_H

#include "common.h"

/* Input dosyasini okur, words dizisine yazar.
 * Basarili: okunan kelime sayisi, Hata: -1 */
int read_input_file(const char *path, WordInfo *words, int max_words);

/* Output dosyasini yazar (sorting_floor -> word_id sirasina gore).
 * Basarili: 0, Hata: -1 */
int write_output_file(const char *path, WordInfo *words, int count);

#endif /* FILE_ISSUES_H */