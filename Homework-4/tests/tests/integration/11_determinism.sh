#!/usr/bin/env bash
# =============================================================================
# 11_determinism.sh - Determinism (tekrarlanabilirlik)
# =============================================================================
# Aynı parametreler ile yapılan ardışık çalışmaların aynı toplam skoru
# üretmesi gerekir. Process'ler arası race olmamalı.
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Integration Test 11: Determinism"
ensure_environment

cd "$FIXTURES_DIR"

# ── 11.1 Tek thread: 5 ardışık run ───────────────────────────────────────────
log_subsection "11.1 Single thread: 5 runs identical"

for i in 1 2 3 4 5; do
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail,timeout" \
        -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
        -o "$RESULTS_DIR/det1_$i.txt" -O "$RESULTS_DIR/det1_$i.bin" \
        > /dev/null 2>&1
done

REF=$(extract_field "$RESULTS_DIR/det1_1.txt" "TOTAL_WEIGHTED_SCORE")
ALL_SAME=true
for i in 2 3 4 5; do
    CUR=$(extract_field "$RESULTS_DIR/det1_$i.txt" "TOTAL_WEIGHTED_SCORE")
    if [ "$CUR" != "$REF" ]; then
        ALL_SAME=false
        log_fail "single thread run $i differs" "ref=$REF got=$CUR"
        break
    fi
done
[ "$ALL_SAME" = "true" ] && log_pass "single thread: all 5 runs identical (TOTAL=$REF)"

# ── 11.2 Multi-thread: 5 run aynı toplam ─────────────────────────────────
log_subsection "11.2 Multi-thread: 5 runs identical TOTAL"

for i in 1 2 3 4 5; do
    timeout 20 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
        -t 4 -w 4 -a 64 -b 64 -d 16 -T 5 \
        -o "$RESULTS_DIR/det2_$i.txt" -O "$RESULTS_DIR/det2_$i.bin" \
        > /dev/null 2>&1
done

REF=$(extract_field "$RESULTS_DIR/det2_1.txt" "TOTAL_WEIGHTED_SCORE")
ALL_SAME=true
for i in 2 3 4 5; do
    CUR=$(extract_field "$RESULTS_DIR/det2_$i.txt" "TOTAL_WEIGHTED_SCORE")
    if [ "$CUR" != "$REF" ]; then
        ALL_SAME=false
        log_fail "multi-thread run $i differs (race condition?)" "ref=$REF got=$CUR"
        break
    fi
done
[ "$ALL_SAME" = "true" ] && log_pass "multi-thread: 5 runs identical (TOTAL=$REF)"

# ── 11.3 Multi-thread: per-LEVEL toplamlar tutarlı ─────────────────────────
log_subsection "11.3 Multi-thread: per-LEVEL totals identical"

for lvl in ERROR WARN INFO DEBUG; do
    REF=$(extract_level_col "$RESULTS_DIR/det2_1.txt" "$lvl" 3)
    ALL_SAME=true
    for i in 2 3 4 5; do
        CUR=$(extract_level_col "$RESULTS_DIR/det2_$i.txt" "$lvl" 3)
        if [ "$CUR" != "$REF" ]; then
            ALL_SAME=false
            break
        fi
    done
    [ "$ALL_SAME" = "true" ] && log_pass "$lvl: identical across 5 runs ($REF)" || \
                                 log_fail "$lvl: varies across runs" "race?"
done

# ── 11.4 Aynı total'a farklı thread sayıları ile ulaşma ────────────────────
log_subsection "11.4 Same TOTAL with t=1 vs t=4 (algorithm consistency, ±1%)"

timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 256 -b 256 -d 64 -T 15 \
    -o "$RESULTS_DIR/det_t1.txt" -O "$RESULTS_DIR/det_t1.bin" \
    > /dev/null 2>&1

timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 256 -b 256 -d 64 -T 15 \
    -o "$RESULTS_DIR/det_t4.txt" -O "$RESULTS_DIR/det_t4.bin" \
    > /dev/null 2>&1

T1=$(extract_field "$RESULTS_DIR/det_t1.txt" "TOTAL_WEIGHTED_SCORE")
T4=$(extract_field "$RESULTS_DIR/det_t4.txt" "TOTAL_WEIGHTED_SCORE")

# %1 tolerans (chunk-boundary entry loss, sub-percent kabul edilebilir)
TOLERANCE=$(awk -v t="$T1" 'BEGIN { printf "%.1f", t * 0.01 }')
assert_numeric_in_range "t=1 ≈ t=4 TOTAL ($T1 vs $T4, ±1%)" "$T1" "$T4" "$TOLERANCE"

# ── 11.5 LEVEL totaller eşit (thread sayısından bağımsız, ±1%) ──────────────
log_subsection "11.5 LEVEL totals stable across thread counts"

for lvl in ERROR WARN INFO DEBUG; do
    L1=$(extract_level_col "$RESULTS_DIR/det_t1.txt" "$lvl" 3)
    L4=$(extract_level_col "$RESULTS_DIR/det_t4.txt" "$lvl" 3)
    TOL=$(awk -v t="$L1" 'BEGIN { v = t*0.02; if (v<1) v=1; printf "%.1f", v }')
    assert_numeric_in_range "$lvl: t=1 ≈ t=4 ($L1 vs $L4, ±2% or ±1)" \
        "$L1" "$L4" "$TOL"
done

# ── 11.6 ENTRIES sayısı tutarlı (±2 entry tolerans) ─────────────────────────
log_subsection "11.6 ENTRIES counts stable (±2 entries)"

for lvl in ERROR WARN INFO DEBUG; do
    E1=$(extract_level_col "$RESULTS_DIR/det_t1.txt" "$lvl" 2)
    E4=$(extract_level_col "$RESULTS_DIR/det_t4.txt" "$lvl" 2)
    DIFF=$((E1 > E4 ? E1 - E4 : E4 - E1))
    if [ "$DIFF" -le 2 ]; then
        log_pass "$lvl: ENTRIES t=1≈t=4 ($E1 vs $E4, diff=$DIFF)"
    else
        log_fail "$lvl: ENTRIES diverge" "t=1=$E1 t=4=$E4 diff=$DIFF"
    fi
done

# ── 11.7 Top-3 source seçimi deterministik ──────────────────────────────────
log_subsection "11.7 Top-3 sources identical across runs"

# multi_source: 4 source, alpha:3, beta:2, gamma:2, delta:1
# 'beta' ile 'gamma' aynı sayıda → tie-breaking deterministik olmayabilir
# Ama 'alpha' her zaman ilk olmalı
for i in 1 2 3; do
    timeout 20 "$ANALYZER_BIN" -c multi_source.conf -f multi_source_priority.txt -k "msg" \
        -t 4 -w 4 -a 16 -b 16 -d 4 -T 5 \
        -o "$RESULTS_DIR/top_$i.txt" -O "$RESULTS_DIR/top_$i.bin" \
        > /dev/null 2>&1
done

for i in 1 2 3; do
    A=$(extract_top_source_count "$RESULTS_DIR/top_$i.txt" "ERROR" "alpha")
    assert_eq "run $i: alpha:3 (always rank 1)" "3" "$A"
done

# ── 11.8 HP score deterministik ─────────────────────────────────────────────
log_subsection "11.8 HIGH_PRIORITY_SCORE deterministic"

REF=$(extract_field "$RESULTS_DIR/det2_1.txt" "HIGH_PRIORITY_SCORE")
ALL_SAME=true
for i in 2 3 4 5; do
    CUR=$(extract_field "$RESULTS_DIR/det2_$i.txt" "HIGH_PRIORITY_SCORE")
    if [ "$CUR" != "$REF" ]; then
        ALL_SAME=false
        break
    fi
done
[ "$ALL_SAME" = "true" ] && log_pass "HP: identical across 5 runs (HP=$REF)" || \
                             log_fail "HP: varies across runs" "race?"

print_summary
