#include "signals_handler.h"

#include <stdio.h>
#include <errno.h>   /* errno — sigchld handler'da kaydetmek için */
#include <unistd.h>  /* write(), STDERR_FILENO                    */
#include <string.h>  /* strlen()                                   */


/* ─────────────────────────────────────────────────────────────
 * FLAG TANIMLARI
 * extern olarak header'da bildirildi, burada gerçek tanım yapılır.
 * volatile: compiler bu değişkenleri register'a almasın (TLPI s.472)
 * sig_atomic_t: tek makinede atomik okuma/yazma (TLPI s.472)
 * ───────────────────────────────────────────────────────────── */
volatile sig_atomic_t got_sigint   = 0;
volatile sig_atomic_t got_sigterm  = 0;
volatile sig_atomic_t workers_done = 0;


/* ─────────────────────────────────────────────────────────────
 * SIGUSR1 HANDLER — sadece parent kullanır
 *
 * Worker işini bitirince parent'a SIGUSR1 gönderir.
 * Handler sadece sayacı artırır — başka hiçbir şey yapmaz.
 * Main loop workers_done == num_workers olunca devam eder.
 *
 * Kaynak: TLPI sayfa 472 — flag set etme kalıbı
 * ───────────────────────────────────────────────────────────── */
static void sigusr1_handler(int sig)
{
    (void)sig;          /* kullanılmayan parametre uyarısını bastır */
    workers_done++;
}

/* ─────────────────────────────────────────────────────────────
 * SIGINT HANDLER — sadece parent kullanır
 *
 * Kullanıcı Ctrl+C bastığında gelir.
 * Handler sadece flag set eder — asıl temizlik main loop'ta yapılır.
 * Handler içinde printf() YASAK olduğu için write() kullanılır.
 *
 * Kaynak: TLPI sayfa 472 — async-signal-safe kalıbı
 * ───────────────────────────────────────────────────────────── */
static void sigint_handler(int sig)
{
    (void)sig;
    got_sigint = 1;
}

/* ─────────────────────────────────────────────────────────────
 * SIGCHLD HANDLER — sadece parent kullanır
 *
 * Child beklenmedik şekilde ölünce kernel bu sinyali gönderir.
 * Zombie oluşmasını engeller — waitpid() WNOHANG ile döngü.
 *
 * ÖNEMLİ: SIGCHLD kuyruğa alınmaz! 3 child aynı anda ölse
 * sadece 1-2 sinyal gelir. Bu yüzden while döngüsü ŞART.
 *
 * ÖNEMLİ: errno kaydedip geri yüklüyoruz — waitpid() errno'yu
 * bozabilir, main program'daki errno kontrollerini etkilemez.
 *
 * Kaynak: TLPI sayfa 600-601, Listing 26-5 (multi_SIGCHLD.c)
 * ───────────────────────────────────────────────────────────── */
static void sigchld_handler(int sig)
{
    (void)sig;

    int saved_errno = errno;   /* errno'yu koru — TLPI Listing 26-5 */
    int status;
    pid_t pid;

    /* Tüm ölmüş child'ları topla — döngü ŞART (TLPI sayfa 600) */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        /* Beklenmedik ölüm: normal akışta worker SIGUSR1 gönderip
         * exit() çağırır. Eğer sinyal ile öldüyse beklenmedik demektir.
         * write() kullanıyoruz — printf() handler'da YASAK (TLPI s.472) */
        if (WIFSIGNALED(status)) {
            char buf[128];
            int len = snprintf(buf, sizeof(buf),
                "[Parent] Worker PID:%d terminated unexpectedly"
                " (exit status: %d).\n",
                (int)pid, WTERMSIG(status));
            write(STDERR_FILENO, buf, len);
        }
    }

    errno = saved_errno;   /* errno'yu geri yükle — TLPI Listing 26-5 */
}


/* ─────────────────────────────────────────────────────────────
 * SIGTERM HANDLER — sadece worker (child) kullanır
 *
 * Parent Ctrl+C alınca tüm worker'lara SIGTERM gönderir.
 * Worker sadece flag set eder — asıl temiz çıkış worker.c'de yapılır:
 *   "[Worker PID:XXXX] SIGTERM received. Partial matches: N. Exiting."
 *
 * Kaynak: TLPI sayfa 472 — flag set etme kalıbı
 * ───────────────────────────────────────────────────────────── */
static void sigterm_handler(int sig)
{
    (void)sig;
    got_sigterm = 1;
}

/* ─────────────────────────────────────────────────────────────
 * SETUP FONKSİYONLARI
 *
 * sigaction() kullanıyoruz — signal() değil.
 * Neden: signal() taşınabilir değil, eski semantikler var.
 * sigaction() her platformda aynı davranışı garantiler.
 *
 * Kaynak: TLPI sayfa 460-461, Bölüm 20.13
 * ───────────────────────────────────────────────────────────── */

void setup_parent_signals(void)
{
    struct sigaction sa;

    /* sa_mask: handler çalışırken başka sinyal bloklama — boş */
    sigemptyset(&sa.sa_mask);

    /* SA_RESTART: sinyal kesilen sistem çağrılarını otomatik yeniden başlat
     * Örneğin: waitpid() EINTR alırsa kendiliğinden tekrar çalışır */
    sa.sa_flags = SA_RESTART;

    /* SIGUSR1: worker bitti bildirimi */
    sa.sa_handler = sigusr1_handler;
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        write(STDERR_FILENO, "sigaction SIGUSR1 failed\n", 25);
        _exit(1);
    }

    /* SIGINT: Ctrl+C */
    sa.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        write(STDERR_FILENO, "sigaction SIGINT failed\n", 24);
        _exit(1);
    }

    /* SIGCHLD: child ölüm bildirimi — zombie önleme */
    sa.sa_handler = sigchld_handler;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        write(STDERR_FILENO, "sigaction SIGCHLD failed\n", 25);
        _exit(1);
    }
}

void setup_worker_signals(void)
{
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    /* SIGTERM: parent'tan dur komutu */
    sa.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        write(STDERR_FILENO, "sigaction SIGTERM failed\n", 25);
        _exit(1);
    }
}
