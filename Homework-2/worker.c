#include "worker.h"

#include <stdio.h>    /* printf(), fprintf()        */
#include <stdlib.h>   /* exit()                     */
#include <unistd.h>   /* fork(), getpid(), getppid()*/
#include <signal.h>   /* kill()                     */
#include <sys/wait.h> /* waitpid()                  */
#include <errno.h>    /* errno                      */

/* ─────────────────────────────────────────────────────────────
 * LAUNCH_WORKERS
 *
 * Her worker için fork() çağırır.
 * Child tarafı: arama yapar, SIGUSR1 gönderir, exit() ile biter.
 * Parent tarafı: PID'leri kaydeder, döner.
 *
 * Kaynak: TLPI sayfa 560-561 (fork kalıbı)
 *         TLPI sayfa 602 (multi_SIGCHLD.c — çoklu fork döngüsü)
 * ───────────────────────────────────────────────────────────── */
void launch_workers(Worker_Partition  partitions[MAX_WORKERS],
                    int               num_of_workers,
                    const char       *pattern,
                    long              min_size,
                    Worker_Result     worker_results[MAX_WORKERS])
{
    for (int i = 0; i < num_of_workers; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            /* fork() başarısız */
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) {
            /* ── CHILD TARAFI ──────────────────────────────────
             * Kaynak: TLPI sayfa 560 — child tarafı fork() sonrası
             * Parent'ın signal handler'larını sıfırla,
             * worker'a özgü handler'ları kur */
            setup_worker_signals();

            Searching_Result result;
            init_searching_result(&result);

            /* Kendi partition'ındaki her klasörü tara */
            for (int j = 0; j < partitions[i].num_of_subdirectories; j++) {
                /* SIGTERM geldi mi? — her klasörden önce kontrol */
                if (got_sigterm) {
                    printf("[Worker PID:%d] SIGTERM received."
                           " Partial matches: %d. Exiting.\n",
                           (int)getpid(), result.match_count);
                    exit(result.match_count % 256);
                }

                search_directory(partitions[i].directories[j],
                                 pattern,
                                 min_size,
                                 &result);
            }

            /* Tüm klasörler bitti — parent'a haber ver */
            kill(getppid(), SIGUSR1);

            /* exit status = eşleşme sayısı (max 255) */
            exit(result.match_count % 256);

            /* Child buradan sonrasına hiç gelmez */
        }

        /* ── PARENT TARAFI ─────────────────────────────────────
         * Child'ın PID'ini kaydet, sonraki worker'ı fork'la */
        worker_results[i].pid         = pid;
        worker_results[i].match_count = 0;  /* waitpid sonrası dolar */
    }
}

/* ─────────────────────────────────────────────────────────────
 * WAIT_FOR_WORKERS
 *
 * Tüm worker'lar SIGUSR1 gönderene kadar pause() ile bekler.
 * SIGINT gelirse terminate_all_workers() çağırır.
 * Sonunda waitpid() ile exit status'ları toplar.
 *
 * Kaynak: TLPI sayfa 590-593 (waitpid + WEXITSTATUS kalıbı)
 * ───────────────────────────────────────────────────────────── */
void wait_for_workers(int           num_of_workers,
                      Worker_Result worker_results[MAX_WORKERS])
{
    /* Tüm worker'lar SIGUSR1 gönderene kadar bekle */
    while (workers_done < num_of_workers) {
        /* SIGINT geldi mi? */
        if (got_sigint) {
            printf("[Parent] SIGINT received."
                   " Terminating all workers...\n");
            kill_workers(num_of_workers, worker_results);
            return;
        }
        pause();   /* Sinyal gelene kadar uyu */
    }

    /* Tüm worker'lar bitti — exit status'ları topla */
    for (int i = 0; i < num_of_workers; i++) {
        int status;
        pid_t pid = waitpid(worker_results[i].pid, &status, 0);

        if (pid == -1) {
            /* Zaten SIGCHLD handler toplamış olabilir */
            if (errno == ECHILD) continue;
            perror("waitpid");
            continue;
        }

        /* exit status = match_count % 256 olarak kaydedilmişti */
        if (WIFEXITED(status))
            worker_results[i].match_count = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            worker_results[i].match_count = 0;
    }
}

/* ─────────────────────────────────────────────────────────────
 * TERMINATE_ALL_WORKERS
 *
 * SIGINT alındığında çağrılır.
 * SIGTERM → 3 saniye bekle → SIGKILL zinciri.
 * Zombie bırakmaz.
 *
 * Kaynak: TLPI sayfa 446 (kill() kullanımı)
 *         TLPI sayfa 590-593 (waitpid kalıbı)
 * ───────────────────────────────────────────────────────────── */
void kill_workers(int           num_of_workers,
                           Worker_Result worker_results[MAX_WORKERS])
{
    /* 1. Tüm worker'lara SIGTERM gönder */
    for (int i = 0; i < num_of_workers; i++) {
        if (worker_results[i].pid > 0)
            kill(worker_results[i].pid, SIGTERM);
    }

    /* 2. 3 saniye bekle */
    sleep(3);

    /* 3. Hâlâ yaşıyanlara SIGKILL */
    for (int i = 0; i < num_of_workers; i++) {
        if (worker_results[i].pid > 0)
            kill(worker_results[i].pid, SIGKILL);
    }

    /* 4. Hepsini topla — zombie bırakma */
    for (int i = 0; i < num_of_workers; i++) {
        int status;
        pid_t pid = waitpid(worker_results[i].pid, &status, 0);
        if (pid == -1) continue;

        if (WIFEXITED(status))
            worker_results[i].match_count = WEXITSTATUS(status);
        else
            worker_results[i].match_count = 0;

        /* PID'i sıfırla — tekrar işlem yapılmasın */
        worker_results[i].pid = 0;
    }
}
