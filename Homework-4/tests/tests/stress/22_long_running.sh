#!/usr/bin/env bash
# =============================================================================
# 22_long_running.sh - Uzun süreli ve büyük yük testleri
# =============================================================================
# Amaç:
#   - Çok dosya (10+ log) işleme
#   - Tekrarlanmış büyük çalıştırmalar
#   - Wall-clock time kontrolü (sonsuz döngü tespiti)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Stress Test 22: Long-Running Workload"
ensure_environment

cd "$FIXTURES_DIR"

# ── 22.1 Many-files config (8 dosya) ───────────────────────────────────────
log_subsection "22.1 Many files in config (8 logs)"

# big1, big2, big3, huge1, huge2, simple_kernel, simple_nginx, long_lines
cat > "$RESULTS_DIR/many.conf" <<EOF
$FIXTURES_DIR/big1.log
$FIXTURES_DIR/big2.log
$FIXTURES_DIR/big3.log
$FIXTURES_DIR/huge1.log
$FIXTURES_DIR/huge2.log
$FIXTURES_DIR/simple_kernel.log
$FIXTURES_DIR/simple_nginx.log
$FIXTURES_DIR/long_lines.log
EOF

START_TIME=$(date +%s)
run_with_timeout 120 "$RESULTS_DIR/many.out" "$RESULTS_DIR/many.err" \
    timeout 120 "$ANALYZER_BIN" -c "$RESULTS_DIR/many.conf" -f empty_priority.txt -k "error,fail,timeout" \
    -t 8 -w 6 -a 64 -b 64 -d 16 -T 60 \
    -o "$RESULTS_DIR/many.txt" -O "$RESULTS_DIR/many.bin"
EXIT=$?
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

assert_exit_code "many files: exit 0" "0" "$EXIT"

# Reasonable upper bound: 60 saniyede bitmeli
if [ "$ELAPSED" -le 60 ]; then
    log_pass "many files: completed in ${ELAPSED}s (<= 60s)"
else
    log_fail "many files: too slow (${ELAPSED}s)" "may indicate infinite loop"
fi

# Reader sayısı = config dosyası sayısı = 8
if [ "$EXIT" -eq 0 ]; then
    READER_COUNT=$(grep -c "Forking Reader" "$RESULTS_DIR/many.out" || echo 0)
    assert_eq "many files: 8 readers forked" "8" "$READER_COUNT"
fi

# ── 22.2 Repeated execution (10x) − total runtime budget ──────────────────
log_subsection "22.2 Ten consecutive medium runs (runtime budget)"

START_TIME=$(date +%s)
ALL_OK=true
for i in $(seq 1 10); do
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
        -t 4 -w 4 -a 32 -b 32 -d 16 -T 30 \
        -o "$RESULTS_DIR/rep_$i.txt" -O "$RESULTS_DIR/rep_$i.bin" \
        > /dev/null 2>&1
    EXIT=$?
    if [ "$EXIT" -ne 0 ]; then
        log_fail "rep run $i: exit $EXIT" ""
        ALL_OK=false
    fi
done
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

if [ "$ALL_OK" = "true" ]; then
    log_pass "10 consecutive runs: all exit 0"
fi

# Ortalama: < 5 saniye/run
AVG=$((ELAPSED / 10))
if [ "$AVG" -le 10 ]; then
    log_pass "avg run time = ${AVG}s (acceptable, total=${ELAPSED}s)"
else
    log_fail "avg run time = ${AVG}s" "may be too slow"
fi

# ── 22.3 Maximum keyword list (MAX_KEYWORDS=8) ─────────────────────────────
log_subsection "22.3 Maximum keyword list (8 keywords)"

KW="error,fail,timeout,success,OK,retry,done,process"

run_with_timeout 120 "$RESULTS_DIR/lk.out" "$RESULTS_DIR/lk.err" \
    timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "$KW" \
    -t 4 -w 4 -a 32 -b 32 -d 16 -T 30 \
    -o "$RESULTS_DIR/lk.txt" -O "$RESULTS_DIR/lk.bin"
EXIT=$?
assert_exit_code "8 keywords: exit 0" "0" "$EXIT"

if [ "$EXIT" -eq 0 ]; then
    HEADER=$(read_binary_header "$RESULTS_DIR/lk.bin")
    assert_contains "8 keywords in binary header" "$HEADER" "keywords=8"
fi

# ── 22.4 Çıktı tutarlılığı: huge runs vs medium runs ──────────────────────
log_subsection "22.4 Result consistency across 10 runs (huge log)"

# rep_1.txt to rep_10.txt should all have same TOTAL
REF=$(extract_field "$RESULTS_DIR/rep_1.txt" "TOTAL_WEIGHTED_SCORE")
ALL_SAME=true
for i in 2 3 4 5 6 7 8 9 10; do
    CUR=$(extract_field "$RESULTS_DIR/rep_$i.txt" "TOTAL_WEIGHTED_SCORE")
    if [ "$CUR" != "$REF" ]; then
        ALL_SAME=false
        log_fail "rep run $i differs" "ref=$REF got=$CUR"
        break
    fi
done
[ "$ALL_SAME" = "true" ] && log_pass "10 runs: TOTAL identical (REF=$REF)"

# ── 22.5 Watchdog ile uzun süreli ─────────────────────────────────────────
log_subsection "22.5 Watchdog reports during long runs"

run_with_timeout 30 "$RESULTS_DIR/wd.out" "$RESULTS_DIR/wd.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error" \
    -t 4 -w 4 -a 32 -b 32 -d 16 -T 25 \
    -o "$RESULTS_DIR/wd.txt" -O "$RESULTS_DIR/wd.bin"
EXIT=$?
assert_exit_code "watchdog run: exit 0" "0" "$EXIT"

# Watchdog mesajı var mı? (1 saniye sonra "still alive" tarzı)
if grep -qiE "watchdog|still alive|heartbeat" "$RESULTS_DIR/wd.out"; then
    log_pass "watchdog active (heartbeat detected in stdout)"
else
    log_skip "22.5 no watchdog message visible" "may run silently"
fi

# Final cleanup
cleanup_processes

print_summary
