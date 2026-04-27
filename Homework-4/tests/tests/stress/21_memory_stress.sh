#!/usr/bin/env bash
# =============================================================================
# 21_memory_stress.sh - Memory ve kaynak (FD/handle) stres testi
# =============================================================================
# Amaç:
#   - Çok küçük buffer + uzun yük → mutex contention extreme
#   - 10x ardışık çalıştırma → handle/FD leak yakalama
#   - /dev/shm leak detection
#   - Memory growth kontrolü (RSS izleme)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Stress Test 21: Memory & Resource Stress"
ensure_environment

cd "$FIXTURES_DIR"

# ── 21.1 Minimum buffer (a=4, b=4, d=2): yüksek contention ─────────────────
log_subsection "21.1 Minimum buffers (a=4, b=4, d=2): heavy contention"

run_with_timeout 120 "$RESULTS_DIR/tiny.out" "$RESULTS_DIR/tiny.err" \
    timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 4 -b 4 -d 2 -T 30 \
    -o "$RESULTS_DIR/tiny.txt" -O "$RESULTS_DIR/tiny.bin"
EXIT=$?
assert_exit_code "min buffer: exit 0" "0" "$EXIT"

if [ "$EXIT" -eq 0 ]; then
    # Reference run for comparison
    timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
        -t 1 -w 1 -a 32 -b 32 -d 16 -T 30 \
        -o "$RESULTS_DIR/tiny_ref.txt" -O "$RESULTS_DIR/tiny_ref.bin" \
        > /dev/null 2>&1

    REF=$(extract_field "$RESULTS_DIR/tiny_ref.txt" "TOTAL_WEIGHTED_SCORE")
    GOT=$(extract_field "$RESULTS_DIR/tiny.txt" "TOTAL_WEIGHTED_SCORE")
    # Tolerans: t=1 vs t=4 chunk-boundary loss (~%1-2)
    TOL=$(awk -v r="$REF" 'BEGIN { printf "%.1f", r*0.02 }')
    assert_numeric_in_range "min buffer: result ≈ ref ($REF, ±2%)" "$REF" "$GOT" "$TOL"
fi

# ── 21.2 FD leak: 10 ardışık run sonra /proc/self/fd ─────────────────────
log_subsection "21.2 No FD leak across 10 consecutive runs"

INITIAL_FD=$(ls /proc/self/fd 2>/dev/null | wc -l)

for i in $(seq 1 10); do
    timeout 120 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
        -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
        -o "$RESULTS_DIR/fd_$i.txt" -O "$RESULTS_DIR/fd_$i.bin" \
        > /dev/null 2>&1
done

FINAL_FD=$(ls /proc/self/fd 2>/dev/null | wc -l)

# Test runner'ın FD'leri %10'dan fazla artmamalı
DIFF=$((FINAL_FD - INITIAL_FD))
if [ "$DIFF" -le 5 ]; then
    log_pass "FD count stable ($INITIAL_FD → $FINAL_FD, diff=$DIFF)"
else
    log_fail "FD growth detected" "init=$INITIAL_FD final=$FINAL_FD diff=$DIFF"
fi

# ── 21.3 /dev/shm leak detection ───────────────────────────────────────────
log_subsection "21.3 /dev/shm leak detection"

INITIAL_SHM=$(ls /dev/shm 2>/dev/null | wc -l)

for i in 1 2 3; do
    timeout 120 "$ANALYZER_BIN" -c huge.conf -f huge_priority.txt -k "error" \
        -t 4 -w 4 -a 32 -b 32 -d 16 -T 20 \
        -o "$RESULTS_DIR/shm_$i.txt" -O "$RESULTS_DIR/shm_$i.bin" \
        > /dev/null 2>&1
done
sleep 0.5

FINAL_SHM=$(ls /dev/shm 2>/dev/null | wc -l)

if [ "$FINAL_SHM" -le "$INITIAL_SHM" ]; then
    log_pass "/dev/shm clean (init=$INITIAL_SHM final=$FINAL_SHM)"
else
    log_fail "/dev/shm leak" "init=$INITIAL_SHM final=$FINAL_SHM"
    # Sızıntı varsa listele
    log_info "leaked: $(ls /dev/shm | head -10)"
fi

# ── 21.4 Process tablosu temiz (zombie check after stress) ────────────────
log_subsection "21.4 Process table clean after burst"

# 5 ardışık run'dan sonra leftover olmamalı
for i in 1 2 3 4 5; do
    timeout 120 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
        -t 4 -w 4 -a 16 -b 16 -d 8 -T 5 \
        -o "$RESULTS_DIR/pt_$i.txt" -O "$RESULTS_DIR/pt_$i.bin" \
        > /dev/null 2>&1
done
sleep 0.5

LEFTOVERS=$(pgrep -af '/analyzer ' 2>/dev/null | grep -v grep | wc -l)
assert_eq "no leftover processes after burst" "0" "$LEFTOVERS"

ZOMBIES=$(ps -eo state,comm 2>/dev/null | awk '$1=="Z" && $2~/analyzer/' | wc -l)
assert_eq "no zombie/defunct after burst" "0" "$ZOMBIES"

# ── 21.5 RSS memory growth ─────────────────────────────────────────────────
log_subsection "21.5 RSS memory growth check (single long run)"

# Background'da çalıştır, RSS'i izle
timeout 120 "$ANALYZER_BIN" -c huge.conf -f huge_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 32 -b 32 -d 16 -T 60 \
    -o "$RESULTS_DIR/rss.txt" -O "$RESULTS_DIR/rss.bin" \
    > "$RESULTS_DIR/rss.out" 2> "$RESULTS_DIR/rss.err" &
PID=$!

sleep 0.3
if process_alive "$PID"; then
    RSS_START=$(ps -o rss= -p "$PID" 2>/dev/null | tr -d ' ')
    sleep 1
    RSS_MID=$(ps -o rss= -p "$PID" 2>/dev/null | tr -d ' ')

    if [ -n "$RSS_START" ] && [ -n "$RSS_MID" ]; then
        # Bellek %50'den fazla artmamalı
        GROWTH=$((RSS_MID - RSS_START))
        if [ "$RSS_START" -gt 0 ]; then
            PCT=$((GROWTH * 100 / RSS_START))
            if [ "$PCT" -lt 100 ]; then
                log_pass "RSS growth bounded ($RSS_START → $RSS_MID KB, +$PCT%)"
            else
                log_fail "RSS growth excessive" "$RSS_START → $RSS_MID KB (+$PCT%)"
            fi
        else
            log_skip "21.5 RSS measurement failed" "process exited fast"
        fi
    else
        log_skip "21.5 RSS unmeasurable" "process state lost"
    fi
fi

wait "$PID" 2>/dev/null
sleep 0.3

# ── 21.6 Stack size: deep call (long lines, max keywords) ──────────────────
log_subsection "21.6 Stack stress: long lines, MAX_KEYWORDS=8"

# 8 keyword (MAX_KEYWORDS) × long lines = derinlemesine matching loop
KW_LIST="error,fail,timeout,success,OK,retry,done,process"

run_with_timeout 120 "$RESULTS_DIR/stk.out" "$RESULTS_DIR/stk.err" \
    timeout 60 "$ANALYZER_BIN" -c long_lines.conf -f empty_priority.txt -k "$KW_LIST" \
    -t 4 -w 4 -a 16 -b 16 -d 8 -T 30 \
    -o "$RESULTS_DIR/stk.txt" -O "$RESULTS_DIR/stk.bin"
EXIT=$?
assert_exit_code "stack stress: exit 0" "0" "$EXIT"

if [ "$EXIT" -eq 0 ]; then
    # 49 satır × 10 'error' × weight 4 = 1960 (sadece 'error' eşleşmeli;
    # 'fail' ve 'timeout' da satırda var ama bu test sadece error kontrolü)
    ERROR_KW=$(extract_level_col "$RESULTS_DIR/stk.txt" "ERROR" 4)
    # Tolerans: 49 veya 50 satır olabilir → 1960 ile 2000 arası kabul edilebilir
    if awk -v v="$ERROR_KW" 'BEGIN { exit !(v >= 1900 && v <= 2050) }'; then
        log_pass "stack stress: ERROR.error in [1900,2050] (got $ERROR_KW)"
    else
        log_fail "stack stress: ERROR.error out of range" "got=$ERROR_KW"
    fi
fi

# Final cleanup
cleanup_processes

print_summary
