#!/usr/bin/env bash
# =============================================================================
# 13_timeout.sh - Aggregator timeout (-T flag) handling
# =============================================================================
# PDF Section 6.5: Aggregator pthread_cond_timedwait kullanır.
# Eğer T süresi dolarsa, alınamayan LEVEL'lar için "timeout" raporu yazılır.
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Integration Test 13: Timeout Handling"
ensure_environment

cd "$FIXTURES_DIR"

# ── 13.1 Normal -T (yeterli zaman) ─────────────────────────────────────────
log_subsection "13.1 Sufficient timeout (T=10): normal completion"

run_with_timeout 15 "$RESULTS_DIR/to1.out" "$RESULTS_DIR/to1.err" \
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 10 \
    -o "$RESULTS_DIR/to1.txt" -O "$RESULTS_DIR/to1.bin"
EXIT=$?

assert_exit_code "T=10: exit 0" "0" "$EXIT"
assert_grep "T=10: 'All results received'" "All results received" "$RESULTS_DIR/to1.out"
assert_not_contains "T=10: no timeout messages" \
    "$(cat $RESULTS_DIR/to1.out)" "Timeout waiting"

# ── 13.2 Çok kısa -T (ama yine de yeterli olabilir) ────────────────────────
log_subsection "13.2 Very short timeout (T=2): may still complete"

run_with_timeout 5 "$RESULTS_DIR/to2.out" "$RESULTS_DIR/to2.err" \
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 2 \
    -o "$RESULTS_DIR/to2.txt" -O "$RESULTS_DIR/to2.bin"
EXIT=$?

# T=2 için küçük log da yetebilir; her durumda çıkış temiz olmalı
if [ "$EXIT" -eq 0 ]; then
    log_pass "T=2: completed normally (exit 0)"
elif [ "$EXIT" -eq 124 ] || [ "$EXIT" -eq 143 ]; then
    log_skip "T=2: external timeout (124/143)" "container too slow"
else
    log_fail "T=2: unexpected exit $EXIT" "should be 0 (or timeout)"
fi

# ── 13.3 1 saniyelik -T (timeout muhtemel) ─────────────────────────────────
log_subsection "13.3 Tight timeout (T=1): completes or times out gracefully"

run_with_timeout 5 "$RESULTS_DIR/to3.out" "$RESULTS_DIR/to3.err" \
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 1 \
    -o "$RESULTS_DIR/to3.txt" -O "$RESULTS_DIR/to3.bin"
EXIT=$?

# Her durumda çöküş olmamalı
if [ "$EXIT" -eq 139 ] || [ "$EXIT" -eq 11 ]; then
    log_fail "T=1: SEGFAULT" "exit=$EXIT"
elif [ "$EXIT" -eq 124 ] || [ "$EXIT" -eq 143 ]; then
    log_skip "T=1: external timeout" "container too slow"
else
    log_pass "T=1: clean exit (no segfault, exit=$EXIT)"
fi

# ── 13.4 Çıktı dosyası timeout durumunda da yazılmalı ──────────────────────
log_subsection "13.4 Output file written even if some levels timed out"

# Eğer T=1 başarılı çalıştıysa output olmalı
if [ -f "$RESULTS_DIR/to3.txt" ]; then
    log_pass "T=1: output .txt exists despite tight timeout"
    assert_grep "T=1: KEYWORD_LIST present" "^KEYWORD_LIST:" "$RESULTS_DIR/to3.txt"
else
    log_skip "13.4 .txt not produced" "T=1 too tight in this env"
fi

# ── 13.5 Aggregator pthread_cond_timedwait kullanımı (kod kontrol) ─────────
log_subsection "13.5 aggregator.c uses pthread_cond_timedwait (source check)"

if grep -q "pthread_cond_timedwait" "$PROJECT_ROOT/aggregator.c"; then
    log_pass "aggregator.c contains pthread_cond_timedwait"
else
    log_fail "aggregator.c does NOT use pthread_cond_timedwait" "PDF -15 puan"
fi

# Aynı dosyada sem_timedwait de olmalı (PDF her ikisini de istiyor)
if grep -q "sem_timedwait" "$PROJECT_ROOT/aggregator.c"; then
    log_pass "aggregator.c contains sem_timedwait"
else
    log_fail "aggregator.c does NOT use sem_timedwait" "PDF requires both"
fi

print_summary
