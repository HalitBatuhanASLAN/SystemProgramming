#!/usr/bin/env bash
# =============================================================================
# 05_per_thread.sh - Per-thread weighted score accounting
# =============================================================================
# Bu testler PDF Section 9'un en kritik kısmını doğrular: TLS destructor'ın
# her worker thread'in kendi katkısını Region C'ye flush ettiğini.
#
# Beklenen davranış:
#   Toplam per-thread skorların toplamı = LEVEL'in WEIGHTED_SCORE'una eşit
#   Eğer bu eşitlik bozuksa: TLS destructor sıralama hatası vardır
#   (transcript'te bu hatayı yakalamıştık)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 05: Per-Thread Score Accounting"
ensure_environment

cd "$FIXTURES_DIR"

# ── 5.1 1 worker → tek thread tüm skoru almalı ──────────────────────────────
log_subsection "5.1 Single worker: thread_0 == LEVEL total"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/pt1.txt" -O "$RESULTS_DIR/pt1.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/pt1.txt"
ERROR_TOTAL=$(extract_level_col "$OUT" "ERROR" 3)
ERROR_T0=$(extract_thread_score "$OUT" "ERROR" 0)
assert_numeric_eq "1 worker: ERROR.thread_0 == ERROR weighted ($ERROR_TOTAL)" \
    "$ERROR_TOTAL" "$ERROR_T0"

# ── 5.2 2 worker: thread skorları toplamı LEVEL toplamına eşit ──────────────
log_subsection "5.2 Two workers: sum(thread_i) == LEVEL total"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/pt2.txt" -O "$RESULTS_DIR/pt2.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/pt2.txt"
for lvl in ERROR WARN INFO DEBUG; do
    LVL_TOTAL=$(extract_level_col "$OUT" "$lvl" 3)
    T0=$(extract_thread_score "$OUT" "$lvl" 0)
    T1=$(extract_thread_score "$OUT" "$lvl" 1)
    SUM=$(awk -v a="$T0" -v b="$T1" 'BEGIN { printf "%.1f", a+b }')
    assert_numeric_eq "$lvl: thread_0 + thread_1 == LEVEL_TOTAL ($LVL_TOTAL)" \
        "$LVL_TOTAL" "$SUM"
done

# ── 5.3 4 worker: tüm thread'lerin toplam katkısı LEVEL'a eşit ──────────────
log_subsection "5.3 Four workers: sum(all threads) == LEVEL total"

timeout 20 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 64 -b 64 -d 16 -T 5 \
    -o "$RESULTS_DIR/pt4.txt" -O "$RESULTS_DIR/pt4.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/pt4.txt"
for lvl in ERROR WARN INFO DEBUG; do
    LVL_TOTAL=$(extract_level_col "$OUT" "$lvl" 3)
    T0=$(extract_thread_score "$OUT" "$lvl" 0)
    T1=$(extract_thread_score "$OUT" "$lvl" 1)
    T2=$(extract_thread_score "$OUT" "$lvl" 2)
    T3=$(extract_thread_score "$OUT" "$lvl" 3)
    SUM=$(awk -v a="$T0" -v b="$T1" -v c="$T2" -v d="$T3" \
        'BEGIN { printf "%.1f", a+b+c+d }')
    assert_numeric_in_range "$lvl: 4-thread sum == LEVEL_TOTAL ($LVL_TOTAL)" \
        "$LVL_TOTAL" "$SUM" "0.05"
done

# ── 5.4 Per-thread skor 0 olmamalı (en az 1 thread çalışmalı) ───────────────
log_subsection "5.4 At least one thread has non-zero contribution"

# big.conf'ta yüzlerce entry var; en az 1 thread'in skoru > 0 olmalı
ALL_ZERO=true
for tid in 0 1 2 3; do
    SCORE=$(extract_thread_score "$OUT" "ERROR" "$tid")
    if [ -z "$SCORE" ]; then continue; fi
    if awk -v s="$SCORE" 'BEGIN { exit !(s > 0) }'; then
        ALL_ZERO=false
        break
    fi
done

if [ "$ALL_ZERO" = "false" ]; then
    log_pass "ERROR: en az bir thread'in skoru > 0"
else
    log_fail "ERROR: tüm thread skorları 0!" \
        "Bu, TLS destructor'ın çağrılmadığı anlamına gelir (Bug #1)"
fi

# ── 5.5 6 worker (yüksek concurrency) per-thread doğrulama ─────────────────
log_subsection "5.5 Six workers: comprehensive per-thread sum check"

timeout 20 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 8 -w 6 -a 128 -b 128 -d 32 -T 5 \
    -o "$RESULTS_DIR/pt6.txt" -O "$RESULTS_DIR/pt6.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/pt6.txt"
for lvl in ERROR WARN INFO DEBUG; do
    LVL_TOTAL=$(extract_level_col "$OUT" "$lvl" 3)
    SUM=0
    for tid in 0 1 2 3 4 5; do
        SCORE=$(extract_thread_score "$OUT" "$lvl" "$tid")
        if [ -z "$SCORE" ]; then SCORE=0; fi
        SUM=$(awk -v s="$SUM" -v v="$SCORE" 'BEGIN { printf "%.1f", s+v }')
    done
    assert_numeric_in_range "$lvl: 6-thread sum == LEVEL_TOTAL ($LVL_TOTAL)" \
        "$LVL_TOTAL" "$SUM" "0.05"
done

# ── 5.6 İndeks tutarlılığı: thread_0..thread_(w-1) hepsi varlığı ────────────
log_subsection "5.6 Thread index continuity (0 .. w-1)"

# 4 worker durumu: thread_0, thread_1, thread_2, thread_3 hepsi olmalı
OUT="$RESULTS_DIR/pt4.txt"
for tid in 0 1 2 3; do
    SCORE=$(extract_thread_score "$OUT" "ERROR" "$tid")
    if [ -n "$SCORE" ]; then
        log_pass "ERROR.thread_$tid present (score=$SCORE)"
    else
        log_fail "ERROR.thread_$tid MISSING" "score not extractable"
    fi
done

# thread_4 (out of range) olmamalı
SCORE=$(extract_thread_score "$OUT" "ERROR" "4")
if [ -z "$SCORE" ] || [ "$SCORE" = "0.0" ]; then
    log_pass "ERROR.thread_4 absent (or 0.0) – correct"
else
    log_fail "ERROR.thread_4 should not exist" "got $SCORE"
fi

# ── 5.7 Per-keyword sum == sum over all keywords for level ──────────────────
log_subsection "5.7 Sum of per-keyword scores == LEVEL weighted total"

# Bir LEVEL satırında: WEIGHTED_SCORE sütunu = sum(keyword sütunları)
# Format: LEVEL ENTRIES WEIGHTED kw1 kw2 ... kwN
timeout 20 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 64 -b 64 -d 16 -T 5 \
    -o "$RESULTS_DIR/pkw.txt" -O "$RESULTS_DIR/pkw.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/pkw.txt"
for lvl in ERROR WARN INFO DEBUG; do
    WEIGHTED=$(extract_level_col "$OUT" "$lvl" 3)
    KW1=$(extract_level_col "$OUT" "$lvl" 4)
    KW2=$(extract_level_col "$OUT" "$lvl" 5)
    KW3=$(extract_level_col "$OUT" "$lvl" 6)
    SUM=$(awk -v a="$KW1" -v b="$KW2" -v c="$KW3" \
        'BEGIN { printf "%.1f", a+b+c }')
    assert_numeric_in_range "$lvl: kw1+kw2+kw3 == WEIGHTED ($WEIGHTED)" \
        "$WEIGHTED" "$SUM" "0.05"
done

print_summary
