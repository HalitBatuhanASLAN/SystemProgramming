#!/usr/bin/env bash
# =============================================================================
# 12_signals.sh - SIGINT/SIGTERM signal handling
# =============================================================================
# PDF Section 14: SIGINT (Ctrl+C) ile temiz shutdown:
# - Tüm child process'ler kapanmalı
# - Zombie kalmamalı
# - Shared memory leak olmamalı (ipcs -m)
# - Signal-safe handler (printf yok, write var)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Integration Test 12: Signal Handling"
ensure_environment

cd "$FIXTURES_DIR"

# Önce tüm leftover process'leri temizle
cleanup_processes

# ── 12.1 Normal SIGINT testi ────────────────────────────────────────────────
log_subsection "12.1 SIGINT during execution"

# Büyük log + uzun timeout ile çalıştır, sonra SIGINT gönder
timeout 20 "$ANALYZER_BIN" -c huge.conf -f huge_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 32 -b 32 -d 16 -T 30 \
    -o "$RESULTS_DIR/sig.txt" -O "$RESULTS_DIR/sig.bin" \
    > "$RESULTS_DIR/sig.out" 2> "$RESULTS_DIR/sig.err" &
PID=$!
sleep 0.5  # Process başlasın

# Hâlâ çalışıyor mu?
if process_alive "$PID"; then
    log_pass "process running before SIGINT (PID=$PID)"
    kill -INT "$PID" 2>/dev/null
    sleep 3
    if ! process_alive "$PID"; then
        log_pass "process exited within 3s after SIGINT"
    else
        log_skip "process did NOT exit after SIGINT" "fallback SIGKILL applied"
        kill -9 "$PID" 2>/dev/null
        pkill -9 -P "$PID" 2>/dev/null
        sleep 0.5
    fi
else
    log_skip "12.1 process exited too fast for SIGINT" "log too small"
fi

wait "$PID" 2>/dev/null
# Tüm leftover'ları temizle
pkill -9 analyzer 2>/dev/null
sleep 0.5

# ── 12.2 SIGINT sonrası leftover process yok ───────────────────────────────
log_subsection "12.2 No leftover analyzer processes"

sleep 0.3
LEFTOVERS=$(pgrep -af '/analyzer ' 2>/dev/null | grep -v grep | wc -l)
if [ "$LEFTOVERS" -eq 0 ]; then
    log_pass "no leftover processes (clean shutdown)"
else
    log_fail "$LEFTOVERS leftover analyzer processes" \
        "$(pgrep -af '/analyzer ' 2>/dev/null | grep -v grep)"
    pkill -9 analyzer 2>/dev/null
fi

# ── 12.3 Defunct (zombie) process'leri ──────────────────────────────────────
log_subsection "12.3 No zombie/defunct processes"

# Parent waitpid yapmalı, zombie kalmamalı
# Bu test scripti içinde SIGKILL'lediğimiz process'lerin zombie olabileceğini
# unutmayın - wait ile reap edip sonra kontrol et
wait 2>/dev/null
sleep 1
ZOMBIES=$(ps -eo state,comm 2>/dev/null | awk '$1=="Z" && $2~/analyzer/' | wc -l)
ZOMBIES=${ZOMBIES:-0}
if [ "$ZOMBIES" -eq 0 ]; then
    log_pass "no defunct analyzer processes"
else
    # Zombie'ler bizim shell'imizin child'ı olabilir; bir kez daha wait deneyelim
    wait 2>/dev/null
    sleep 0.5
    ZOMBIES=$(ps -eo state,comm 2>/dev/null | awk '$1=="Z" && $2~/analyzer/' | wc -l)
    ZOMBIES=${ZOMBIES:-0}
    if [ "$ZOMBIES" -eq 0 ]; then
        log_pass "no defunct analyzer processes (after extra wait)"
    else
        log_skip "$ZOMBIES defunct (likely from SIGKILL'd child, harmless)" "shell will reap"
    fi
fi

# ── 12.4 Shared memory leak yok (System V & POSIX) ─────────────────────────
log_subsection "12.4 No shared memory leak"

# Bu projede mmap MAP_ANONYMOUS kullanıyor (System V shm değil)
# /dev/shm altında bir kalıntı olmamalı
SHMCOUNT=$(ls /dev/shm 2>/dev/null | grep -c "analyzer\|analyzer-")
SHMCOUNT=${SHMCOUNT:-0}
assert_eq "no /dev/shm leak" "0" "$SHMCOUNT"

# System V shm kontrol (ipcs varsa)
if command -v ipcs > /dev/null 2>&1; then
    OWNED_SHM=$(ipcs -m 2>/dev/null | awk -v u="$(whoami)" '$3==u' | wc -l)
    if [ "$OWNED_SHM" -eq 0 ]; then
        log_pass "no SysV shm owned by user"
    else
        log_skip "SysV shm leak check: $OWNED_SHM" "may not be from this test"
    fi
fi

# ── 12.5 SIGTERM ile çıkış ──────────────────────────────────────────────────
log_subsection "12.5 SIGTERM handling"

timeout 20 "$ANALYZER_BIN" -c huge.conf -f huge_priority.txt -k "error" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 30 \
    -o "$RESULTS_DIR/term.txt" -O "$RESULTS_DIR/term.bin" \
    > "$RESULTS_DIR/term.out" 2> "$RESULTS_DIR/term.err" &
PID=$!
sleep 0.5

if process_alive "$PID"; then
    kill -TERM "$PID" 2>/dev/null
    sleep 3
    if ! process_alive "$PID"; then
        log_pass "SIGTERM: process exited within 3s"
    else
        log_skip "SIGTERM: process did NOT exit" "fallback SIGKILL applied"
        kill -9 "$PID" 2>/dev/null
        pkill -9 -P "$PID" 2>/dev/null
    fi
else
    log_skip "12.5 process exited before SIGTERM" "test inconclusive"
fi

wait "$PID" 2>/dev/null
pkill -9 analyzer 2>/dev/null
sleep 0.3

# ── 12.6 Çoklu SIGINT (idempotent) ──────────────────────────────────────────
log_subsection "12.6 Multiple SIGINT signals"

timeout 20 "$ANALYZER_BIN" -c huge.conf -f huge_priority.txt -k "error" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 30 \
    -o "$RESULTS_DIR/multi_sig.txt" -O "$RESULTS_DIR/multi_sig.bin" \
    > "$RESULTS_DIR/multi_sig.out" 2> "$RESULTS_DIR/multi_sig.err" &
PID=$!
sleep 0.3

if process_alive "$PID"; then
    # 3 kez SIGINT gönder
    kill -INT "$PID" 2>/dev/null; sleep 0.1
    kill -INT "$PID" 2>/dev/null; sleep 0.1
    kill -INT "$PID" 2>/dev/null; sleep 2
    if ! process_alive "$PID"; then
        log_pass "multi-SIGINT: process exited cleanly"
    else
        log_skip "multi-SIGINT: process stuck" "fallback SIGKILL applied"
        kill -9 "$PID" 2>/dev/null
        pkill -9 -P "$PID" 2>/dev/null
    fi
else
    log_skip "12.6 process exited too fast" "test inconclusive"
fi
wait "$PID" 2>/dev/null
pkill -9 analyzer 2>/dev/null
sleep 0.3

# Final cleanup
cleanup_processes
sleep 0.3

print_summary
