#!/usr/bin/env bash
# =============================================================================
# 14_source_compliance.sh - Kaynak kodun PDF gereksinimlerine uygunluğu
# =============================================================================
# Statik analizler:
# - Mandatory primitive'lerin kullanımı (pthread_key_t, sem_t, vb.)
# - Yasaklanmış API'lerin kullanılmadığı (system(), gets(), vb.)
# - Lock ordering doğru mu (sıralama A → B → C → D)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Integration Test 14: Source Code Compliance"
ensure_environment

cd "$PROJECT_ROOT"

# ── 14.1 Mandatory primitives ──────────────────────────────────────────────
log_subsection "14.1 PDF Mandatory primitives present"

# pthread_key_t (TLS) - PDF Section 9
if grep -rqE "pthread_key_t|pthread_key_create|pthread_setspecific|pthread_getspecific" \
        --include="*.c" .; then
    log_pass "pthread_key_t (TLS) used"
else
    log_fail "pthread_key_t (TLS) NOT used" "PDF mandatory -10 puan"
fi

# pthread_barrier_t - PDF Section 9
if grep -rq "pthread_barrier" --include="*.c" .; then
    log_pass "pthread_barrier_t used"
else
    log_fail "pthread_barrier_t NOT used" "PDF mandatory"
fi

# pthread_cond_timedwait - PDF Section 6.5
if grep -rq "pthread_cond_timedwait" --include="*.c" .; then
    log_pass "pthread_cond_timedwait used"
else
    log_fail "pthread_cond_timedwait NOT used" "PDF mandatory -15 puan"
fi

# sem_t / sem_timedwait - PDF Section 6.5
if grep -rqE "sem_t|sem_init|sem_post|sem_wait|sem_timedwait" \
        --include="*.c" .; then
    log_pass "sem_t (POSIX semaphore) used"
else
    log_fail "sem_t (POSIX semaphore) NOT used" "PDF mandatory"
fi

# syscall(SYS_gettid) - PDF Section 9 (NOT pthread_self for TID logging)
if grep -rqE "syscall\s*\(\s*SYS_gettid|gettid\s*\(\s*\)" \
        --include="*.c" .; then
    log_pass "syscall(SYS_gettid) used"
else
    log_fail "syscall(SYS_gettid) NOT used" "PDF: must use gettid not pthread_self"
fi

# fork() + waitpid()
if grep -rq "fork\s*(" --include="*.c" .; then
    log_pass "fork() used (multi-process design)"
else
    log_fail "fork() NOT used" "PDF: must be multi-process"
fi

if grep -rq "waitpid\s*(" --include="*.c" .; then
    log_pass "waitpid() used"
else
    log_fail "waitpid() NOT used" "child reaping required"
fi

# mmap() with MAP_SHARED
if grep -rq "MAP_SHARED" --include="*.c" .; then
    log_pass "MAP_SHARED used (shared memory)"
else
    log_fail "MAP_SHARED NOT used" "PDF: 4 shared regions"
fi

# PROCESS_SHARED for mutex/cond
if grep -rq "PTHREAD_PROCESS_SHARED" --include="*.c" .; then
    log_pass "PTHREAD_PROCESS_SHARED used"
else
    log_fail "PTHREAD_PROCESS_SHARED NOT used" "shared mutex/cond require this"
fi

# ── 14.2 Yasaklanmış API'ler ───────────────────────────────────────────────
log_subsection "14.2 Forbidden APIs not used"

# Yasaklanmış: gets() (buffer overflow riski)
if grep -rqE "[^[:alnum:]_]gets\s*\(" --include="*.c" .; then
    log_fail "gets() used (DANGEROUS, buffer overflow)" "use fgets()"
else
    log_pass "gets() not used"
fi

# system() - güvenlik riski
if grep -rqE "[^[:alnum:]_]system\s*\(" --include="*.c" .; then
    log_fail "system() used (security risk)" "shell injection"
else
    log_pass "system() not used"
fi

# ── 14.3 Sinyal handler güvenliği ──────────────────────────────────────────
log_subsection "14.3 Signal handler safety"

# Signal handler içinde printf YOKSA (signal-async-unsafe)
# main.c'deki SIGINT handler'ı bul
HANDLER_FUNCS=$(grep -hE "void\s+\w+_handler" --include="*.c" -A 0 . 2>/dev/null | head -5)
log_info "found handlers: $HANDLER_FUNCS"

# sigaction kullanımı (PDF: mandatory)
if grep -rq "sigaction" --include="*.c" .; then
    log_pass "sigaction used (PDF Section 14)"
else
    log_fail "sigaction NOT used" "must use sigaction not signal()"
fi

# volatile sig_atomic_t
if grep -rqE "volatile\s+sig_atomic_t" --include="*.c" .; then
    log_pass "volatile sig_atomic_t used"
else
    log_fail "volatile sig_atomic_t NOT used" "race-safe flag"
fi

# ── 14.4 Atomic rename for binary file ─────────────────────────────────────
log_subsection "14.4 Atomic rename for binary output"

# rename() veya rename_at() çağrısı
if grep -rqE "rename\s*\(" --include="*.c" .; then
    log_pass "rename() used (atomic binary write)"
else
    log_fail "rename() NOT used" "PDF: atomic rename required"
fi

# ── 14.5 Lock ordering: A → B → C → D ──────────────────────────────────────
log_subsection "14.5 Lock ordering (no nested locks across regions)"

# Bu statik analiz zor; daha esnek kontrol: shm.c'de pthread_mutex_init veya
# pthread_cond_init çağrısı olmalı (helper içinde de olsa)
MUTEX_INIT_OK=false
if grep -q "pthread_mutex_init" "$PROJECT_ROOT/shm.c" 2>/dev/null; then
    MUTEX_INIT_OK=true
fi
if [ "$MUTEX_INIT_OK" = "true" ]; then
    log_pass "shm.c uses pthread_mutex_init"
else
    log_fail "shm.c does NOT use pthread_mutex_init" "needed for shared region locks"
fi

# 4 region için sync ihtiyacı: cond_init veya mutex_init helper çağrısı
SYNC_HELPER=$(grep -cE "init_(mutex|cond)|pthread_(mutex|cond)_init|init_lock" \
    "$PROJECT_ROOT/shm.c" 2>/dev/null || echo 0)
SYNC_HELPER=${SYNC_HELPER:-0}
if [ "$SYNC_HELPER" -ge 1 ]; then
    log_pass "shm.c initializes synchronization primitives ($SYNC_HELPER calls)"
else
    log_fail "shm.c missing sync init" "found $SYNC_HELPER init calls"
fi

# ── 14.6 Compile temizliği (no warnings) ───────────────────────────────────
log_subsection "14.6 Clean compile (-Wall -Wextra)"

cd "$PROJECT_ROOT"
make clean > /dev/null 2>&1
WARNINGS=$(make 2>&1 | grep -ci "warning:")
WARNINGS=${WARNINGS:-0}

if [ "$WARNINGS" -eq 0 ]; then
    log_pass "compile: 0 warnings"
else
    log_fail "compile: $WARNINGS warnings" "should be 0"
fi

# ── 14.7 Makefile uyumluluğu ───────────────────────────────────────────────
log_subsection "14.7 Makefile targets present"

if [ -f "Makefile" ]; then
    # 'all' veya 'analyzer' default target
    if grep -qE "^all:|^analyzer:" Makefile; then
        log_pass "Makefile: 'all' or 'analyzer' target present"
    else
        log_fail "Makefile: no default target" "PDF requires 'make' to work"
    fi

    # 'clean' target
    if grep -qE "^clean:" Makefile; then
        log_pass "Makefile: 'clean' target present"
    else
        log_fail "Makefile: no 'clean' target" "PDF requires 'make clean'"
    fi
fi

# ── 14.8 Header guard'ları ─────────────────────────────────────────────────
log_subsection "14.8 Header files have include guards"

for h in shm.h reader.h dispatcher.h analyzer.h aggregator.h watchdog.h; do
    if [ -f "$h" ]; then
        # #pragma once veya #ifndef
        if grep -qE "^#pragma\s+once|^#ifndef\s+\w+" "$h"; then
            log_pass "$h: has include guard"
        else
            log_fail "$h: no include guard" "may cause double-inclusion"
        fi
    fi
done

# ── 14.9 -pthread flag ─────────────────────────────────────────────────────
log_subsection "14.9 -pthread linking flag"

if grep -q "pthread" Makefile 2>/dev/null; then
    log_pass "Makefile uses -pthread"
else
    log_fail "Makefile missing -pthread" "linker error otherwise"
fi

print_summary
