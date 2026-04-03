#ifndef COMMON_H
#define COMMON_H

/* POSIX API'leri icin gerekli (sigaction, usleep, kill vs.) */
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

/* --- Sabitler --- */
#define MAX_WORD_LEN      64      /* Bir kelimenin max uzunlugu */
#define MAX_WORDS         256     /* Sistemdeki max kelime sayisi */
#define MAX_FLOORS        64      /* Max kat sayisi */
#define MAX_ELEVATOR_CAP  64      /* Asansor max kapasite */
#define MAX_QUEUE_SIZE    512     /* Asansor istek kuyrugu max boyutu */
#define MAX_PROCESSES     512     /* Toplam max child process sayisi */

/* Asansor yon sabitleri */
#define DIR_UP    1
#define DIR_DOWN -1
#define DIR_IDLE  0

/* --- Komut satiri parametreleri --- */
typedef struct {
    int num_floors;                   /* -f: kat sayisi */
    int word_carriers_per_floor;      /* -w: kat basina word-carrier */
    int letter_carriers_per_floor;    /* -l: kat basina letter-carrier */
    int sorting_processes_per_floor;  /* -s: kat basina sorting process */
    int max_words_per_floor;          /* -c: kat basina max aktif kelime */
    int delivery_elevator_capacity;   /* -d: delivery asansor kapasitesi */
    int reposition_elevator_capacity; /* -r: reposition asansor kapasitesi */
    char input_file[256];             /* -i: girdi dosyasi yolu */
    char output_file[256];            /* -o: cikti dosyasi yolu */
} SystemConfig;

/* --- Harf gorevi (karakter tasima bilgisi) --- */
typedef struct {
    int word_id;          /* Ait oldugu kelimenin ID'si */
    char character;       /* Tasinacak karakter */
    int original_index;   /* Kelimedeki orijinal pozisyon */
    int src_floor;        /* Kaynak kat (arrival floor) */
    int dest_floor;       /* Hedef kat (sorting floor) */
    int claimed;          /* 1 ise bir letter-carrier tarafindan alinmis */
    int delivered;        /* 1 ise teslim edilmis */
} CharTask;

/* --- Kelime bilgisi --- */
typedef struct {
    int word_id;                          /* Benzersiz kelime ID */
    char word[MAX_WORD_LEN];              /* Orijinal kelime */
    int word_len;                         /* Kelime uzunlugu */
    int sorting_floor;                    /* Hangi katta siralanacak */
    int arrival_floor;                    /* Hangi kata getirildi (-1: henuz atanmadi) */

    /* Durum flag'leri */
    int claimed;                          /* Bir word-carrier tarafindan secildi mi */
    int admitted;                         /* Sisteme kabul edildi mi */
    int completed;                        /* Siralama tamamlandi mi */

    /* Siralama alani */
    char sorting_area[MAX_WORD_LEN];      /* Harflerin yerlestirildig alan */
    int occupied[MAX_WORD_LEN];           /* sorting_area[i] dolu mu */
    int fixed[MAX_WORD_LEN];              /* sorting_area[i] sabitlenmis mi */

    /* Harf gorevleri */
    CharTask char_tasks[MAX_WORD_LEN];    /* Her harf icin bir gorev */
    int num_char_tasks;                   /* Toplam harf gorevi sayisi */

    /* Per-word senkronizasyon */
    pthread_mutex_t word_mutex;           /* Kelime bazinda kilit */
} WordInfo;

/* --- Asansor istegi --- */
typedef struct {
    int requester_type;   /* 0: letter-carrier (delivery), 1: idle carrier (reposition) */
    int from_floor;       /* Istek yapan kat */
    int to_floor;         /* Hedef kat */
    int carrier_id;       /* Hangi carrier istedi */
    int word_id;          /* Tasidigi kelimenin ID'si (delivery icin) */
    char character;       /* Tasidigi karakter (delivery icin) */
    int original_index;   /* Karakterin orijinal indexi (delivery icin) */
    int served;           /* 1 ise istek karsilandi */
} ElevatorRequest;

/* --- Asansor durumu --- */
typedef struct {
    int current_floor;                          /* Suanki kat */
    int direction;                              /* DIR_UP, DIR_DOWN, DIR_IDLE */
    int capacity;                               /* Max kapasite */
    int current_load;                           /* Suanki yuk */

    /* Istek kuyrugu */
    ElevatorRequest queue[MAX_QUEUE_SIZE];
    int queue_size;                             /* Kuyrukdaki istek sayisi */

    /* Senkronizasyon */
    pthread_mutex_t elev_mutex;                 /* Asansor kilidi */
    pthread_cond_t elev_cond;                   /* Asansor condition variable */
    sem_t request_sem;                          /* Yeni istek sinyali */
} ElevatorState;

/* --- Kat bilgisi --- */
typedef struct {
    int floor_id;                               /* Kat numarasi */
    int active_word_count;                       /* Aktif kelime sayisi (arrival + sorting) */
    int letter_carrier_count;                    /* Kattaki letter-carrier sayisi */

    pthread_mutex_t floor_mutex;                /* Kat kilidi */
    pthread_cond_t floor_cond;                  /* Kat condition variable */
} Floor;

/* --- Paylasilan bellegin root yapisi --- */
typedef struct {
    SystemConfig config;                        /* Sistem konfigurasyonu */

    /* Kelimeler */
    WordInfo words[MAX_WORDS];
    int total_words;                            /* Toplam kelime sayisi */
    int completed_words;                        /* Tamamlanan kelime sayisi */

    /* Katlar */
    Floor floors[MAX_FLOORS];

    /* Asansorler */
    ElevatorState delivery_elevator;
    ElevatorState reposition_elevator;

    /* Round-robin kelime secimi */
    int round_robin_index;                      /* Siradaki kelime indexi */
    pthread_mutex_t round_robin_mutex;          /* Round-robin kilidi */

    /* Istatistikler */
    int total_retries;                          /* Toplam tekrar denemesi */
    int total_chars_transported;                /* Tasinan toplam karakter */
    int delivery_elevator_ops;                  /* Delivery asansor islem sayisi */
    int reposition_elevator_ops;                /* Reposition asansor islem sayisi */
    pthread_mutex_t stats_mutex;                /* Istatistik kilidi */

    /* Sistem durumu */
    volatile int system_running;                /* 0 olursa tum process'ler durur */
    volatile int all_words_admitted;            /* Tum kelimeler sisteme alindi mi */

    /* Parent PID (child'lar bilsin diye) */
    pid_t parent_pid;

    /* Child process PID'leri (cleanup icin) */
    pid_t child_pids[MAX_PROCESSES];
    int num_children;
    pthread_mutex_t children_mutex;             /* Child listesi kilidi */
} SharedData;

#endif /* COMMON_H */