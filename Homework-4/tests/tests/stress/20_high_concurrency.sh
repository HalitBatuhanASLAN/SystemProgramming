#!/usr/bin/env bash
# =============================================================================
# 20_high_concurrency.sh - Yüksek concurrency stres testi
# =============================================================================
# Amaç: Çok sayıda thread ve worker ile race condition / deadlock yakalamak.
# Beklenen: Sonuç 1-thread sonucu ile aynı (entry kaybı veya double-count yok).
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Stress Test 20: High Concurrency"
ensure_environment

cd "$FIXTURES_DIR"

# ── 20.1 Reference run (t=1, w=1) ──────────────────────────────────────────
log_subsection "20.1 Reference run (t=1, w=1) - ground truth"

run_with_timeout 120 "$RESULTS_DIR/ref.out" "$RESULTS_DIR/ref.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 32 -b 32 -d 16 -T 90 \
    -o "$RESULTS_DIR/ref.txt" -O "$RESULTS_DIR/ref.bin"
EXIT=$?
assert_exit_code "ref run: exit 0" "0" "$EXIT"

REF_TOTAL=$(extract_field "$RESULTS_DIR/ref.txt" "TOTAL_WEIGHTED_SCORE")
REF_HP=$(extract_field "$RESULTS_DIR/ref.txt" "HIGH_PRIORITY_SCORE")
log_info "reference TOTAL=$REF_TOTAL HP=$REF_HP"

# ── 20.2 Medium concurrency (t=4, w=4) ─────────────────────────────────────
log_subsection "20.2 Medium concurrency (t=4, w=4)"

run_with_timeout 120 "$RESULTS_DIR/m.out" "$RESULTS_DIR/m.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 4 -a 64 -b 64 -d 16 -T 30 \
    -o "$RESULTS_DIR/m.txt" -O "$RESULTS_DIR/m.bin"
EXIT=$?
assert_exit_code "medium: exit 0" "0" "$EXIT"

M_TOTAL=$(extract_field "$RESULTS_DIR/m.txt" "TOTAL_WEIGHTED_SCORE")
M_HP=$(extract_field "$RESULTS_DIR/m.txt" "HIGH_PRIORITY_SCORE")

# Tolerans: chunk-boundary entry loss ~%1
TOL=$(awk -v r="$REF_TOTAL" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
assert_numeric_in_range "medium: TOTAL ≈ ref ($REF_TOTAL, ±2%)" "$REF_TOTAL" "$M_TOTAL" "$TOL"
assert_numeric_in_range "medium: HP ≈ ref ($REF_HP, ±2%)"       "$REF_HP"    "$M_HP"    "$TOL"

# Per-LEVEL toplamlar tutarlı
for lvl in ERROR WARN INFO DEBUG; do
    R=$(extract_level_col "$RESULTS_DIR/ref.txt" "$lvl" 3)
    M=$(extract_level_col "$RESULTS_DIR/m.txt" "$lvl" 3)
    LTOL=$(awk -v r="$R" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
    assert_numeric_in_range "medium: $lvl ≈ ref ($R, ±2%)" "$R" "$M" "$LTOL"
done

# ── 20.3 High concurrency (t=8, w=6) ───────────────────────────────────────
log_subsection "20.3 High concurrency (t=8, w=6)"

run_with_timeout 120 "$RESULTS_DIR/h.out" "$RESULTS_DIR/h.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 8 -w 6 -a 128 -b 128 -d 32 -T 30 \
    -o "$RESULTS_DIR/h.txt" -O "$RESULTS_DIR/h.bin"
EXIT=$?
assert_exit_code "high: exit 0" "0" "$EXIT"

H_TOTAL=$(extract_field "$RESULTS_DIR/h.txt" "TOTAL_WEIGHTED_SCORE")
H_HP=$(extract_field "$RESULTS_DIR/h.txt" "HIGH_PRIORITY_SCORE")

TOL=$(awk -v r="$REF_TOTAL" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
assert_numeric_in_range "high: TOTAL ≈ ref ($REF_TOTAL, ±2%)" "$REF_TOTAL" "$H_TOTAL" "$TOL"
assert_numeric_in_range "high: HP ≈ ref ($REF_HP, ±2%)"       "$REF_HP"    "$H_HP"    "$TOL"

# ── 20.4 Maksimum concurrency (t=16, w=8) ──────────────────────────────────
log_subsection "20.4 Max concurrency (t=16, w=8)"

run_with_timeout 120 "$RESULTS_DIR/max.out" "$RESULTS_DIR/max.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 16 -w 8 -a 256 -b 256 -d 64 -T 60 \
    -o "$RESULTS_DIR/max.txt" -O "$RESULTS_DIR/max.bin"
EXIT=$?
assert_exit_code "max: exit 0" "0" "$EXIT"

if [ "$EXIT" -eq 0 ]; then
    MAX_TOTAL=$(extract_field "$RESULTS_DIR/max.txt" "TOTAL_WEIGHTED_SCORE")
    MAX_HP=$(extract_field "$RESULTS_DIR/max.txt" "HIGH_PRIORITY_SCORE")

    TOL=$(awk -v r="$REF_TOTAL" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
    assert_numeric_in_range "max: TOTAL ≈ ref ($REF_TOTAL, ±2%)" "$REF_TOTAL" "$MAX_TOTAL" "$TOL"
    assert_numeric_in_range "max: HP ≈ ref ($REF_HP, ±2%)"       "$REF_HP"    "$MAX_HP"    "$TOL"
fi

# ── 20.5 Küçük buffer + çok thread (queue contention) ──────────────────────
log_subsection "20.5 Small buffers, many threads (queue contention)"

# Reference: big.conf (1000 satır) ile küçük buffer testi
timeout 60 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 32 -b 32 -d 16 -T 30 \
    -o "$RESULTS_DIR/sb_ref.txt" -O "$RESULTS_DIR/sb_ref.bin" \
    > /dev/null 2>&1

SB_REF=$(extract_field "$RESULTS_DIR/sb_ref.txt" "TOTAL_WEIGHTED_SCORE")

# A=4 (min), B=4, D=2 (min) ile darboğaz; 4 thread / 2 worker
run_with_timeout 120 "$RESULTS_DIR/sb.out" "$RESULTS_DIR/sb.err" \
    timeout 90 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 4 -w 2 -a 4 -b 4 -d 2 -T 60 \
    -o "$RESULTS_DIR/sb.txt" -O "$RESULTS_DIR/sb.bin"
EXIT=$?
assert_exit_code "small buf + many threads: exit 0" "0" "$EXIT"

if [ "$EXIT" -eq 0 ]; then
    SB_TOTAL=$(extract_field "$RESULTS_DIR/sb.txt" "TOTAL_WEIGHTED_SCORE")
    # ±2% tolerans (chunk-boundary loss)
    TOL=$(awk -v r="$SB_REF" 'BEGIN { printf "%.1f", r*0.02 }')
    assert_numeric_in_range "small buffer: TOTAL ≈ ref ($SB_REF, ±2%)" \
        "$SB_REF" "$SB_TOTAL" "$TOL"
fi

# ── 20.6 Tek thread + büyük buffer (no contention) ─────────────────────────
log_subsection "20.6 Single thread + huge buffers (sanity)"

run_with_timeout 120 "$RESULTS_DIR/lb.out" "$RESULTS_DIR/lb.err" \
    timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 1024 -b 1024 -d 256 -T 30 \
    -o "$RESULTS_DIR/lb.txt" -O "$RESULTS_DIR/lb.bin"
EXIT=$?
assert_exit_code "large buffer: exit 0" "0" "$EXIT"

LB_TOTAL=$(extract_field "$RESULTS_DIR/lb.txt" "TOTAL_WEIGHTED_SCORE")
TOL=$(awk -v r="$REF_TOTAL" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
assert_numeric_in_range "large buffer: TOTAL ≈ ref" "$REF_TOTAL" "$LB_TOTAL" "$TOL"

# ── 20.7 Repeat stress: 5 ardışık high-concurrency run ─────────────────────
log_subsection "20.7 5 consecutive high-concurrency runs"

ALL_SAME=true
TOL=$(awk -v r="$REF_TOTAL" 'BEGIN { v=r*0.02; if (v<5) v=5; printf "%.1f", v }')
for i in 1 2 3 4 5; do
    run_with_timeout 120 "/dev/null" "/dev/null" \
        timeout 120 "$ANALYZER_BIN" -c big.conf -f big_priority.txt -k "error,fail,timeout" \
        -t 8 -w 6 -a 64 -b 64 -d 16 -T 30 \
        -o "$RESULTS_DIR/r${i}.txt" -O "$RESULTS_DIR/r${i}.bin"

    EXIT=$?
    if [ "$EXIT" -ne 0 ]; then
        log_fail "stress run $i exited $EXIT" ""
        ALL_SAME=false
        continue
    fi

    CUR=$(extract_field "$RESULTS_DIR/r${i}.txt" "TOTAL_WEIGHTED_SCORE")
    # ±2% tolerans
    if ! awk -v c="$CUR" -v r="$REF_TOTAL" -v t="$TOL" \
            'BEGIN { d=c-r; if(d<0)d=-d; exit !(d<=t) }'; then
        ALL_SAME=false
        log_fail "stress run $i: TOTAL diverges (got $CUR vs ref $REF_TOTAL ±$TOL)" ""
    fi
done

[ "$ALL_SAME" = "true" ] && \
    log_pass "5 consecutive stress runs: all within ±2% of reference"

# ── 20.8 No leftover processes after stress ────────────────────────────────
log_subsection "20.8 No leftover after stress"

sleep 0.5
LEFTOVERS=$(pgrep -af '/analyzer ' 2>/dev/null | grep -v grep | wc -l)
assert_eq "no leftover analyzer processes" "0" "$LEFTOVERS"

print_summary
