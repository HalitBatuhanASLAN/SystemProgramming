#!/usr/bin/env bash
# =============================================================================
# 02_keyword_counting.sh - Keyword counting (sliding window)
# =============================================================================
# Bu testler PDF'in özellikle vurguladığı OVERLAPPING keyword sayımının doğru
# çalıştığını kontrol eder. PDF Section 9 (Counting Keywords): "Overlapping
# matches must be counted separately."
#
# Beklenen Davranış:
#   "errorerorerror" içinde "error" → index 0 ve index 8 → 2 kez
#   "aaaaa"          içinde "aa"    → index 0,1,2,3       → 4 kez
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 02: Keyword Counting (Sliding Window)"
ensure_environment

cd "$FIXTURES_DIR"

# ── 2.1 Overlapping match testi ──────────────────────────────────────────────
log_subsection "2.1 Overlapping keyword matches"

timeout 20 "$ANALYZER_BIN" -c overlap.conf -f overlap_priority.txt -k "error,aa" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/overlap.txt" -O "$RESULTS_DIR/overlap.bin" \
    > "$RESULTS_DIR/overlap.out" 2>&1

OUT="$RESULTS_DIR/overlap.txt"
assert_file_exists "overlap output exists" "$OUT"

# 'errorerorerror' (1 satır): 'error' = 2 (sliding window)
# 'aaaaa' (1 satır):           'aa'    = 4 (sliding window)
# 'no match here' (1 satır):    0
# ERROR weight = 4
# Beklenen: error toplamı = 2*4 = 8, aa toplamı = 4*4 = 16, total = 24

ERROR_KW=$(extract_level_col "$OUT" "ERROR" 4)   # error column (4. kolon: error)
AA_KW=$(extract_level_col "$OUT" "ERROR" 5)      # aa column (5. kolon)
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")

assert_numeric_eq "ERROR.error = 8.0 (2 occurrences × weight 4)" "8.0" "$ERROR_KW"
assert_numeric_eq "ERROR.aa = 16.0 (4 occurrences × weight 4)"   "16.0" "$AA_KW"
assert_numeric_eq "TOTAL_WEIGHTED_SCORE = 24.0"                  "24.0" "$TOTAL"

# ── 2.2 Non-overlapping (basit count) ────────────────────────────────────────
log_subsection "2.2 Non-overlapping simple count"

# error_only.log içinde: 4 satır 'error' (her satır 1 kez), 1 satır 'no match'
# Beklenen ERROR.error: 4 satır × 1 occurrence × weight 4 = ?
# Aslında satır ayrımı: 'error' geçen satır sayısı 4, her birinde 1 occurrence
# Dosya:
#   [ERROR] error
#   [ERROR] fail
#   [ERROR] timeout
#   [ERROR] error fail timeout  ← bu satırda 1 'error', 1 'fail', 1 'timeout'
#   [ERROR] no match
# Toplam 'error': 1 + 0 + 0 + 1 + 0 = 2  → 2*4 = 8
# Toplam 'fail':  0 + 1 + 0 + 1 + 0 = 2  → 2*4 = 8
# Toplam 'timeout': 0 + 0 + 1 + 1 + 0 = 2 → 2*4 = 8
# weighted total = 24

timeout 20 "$ANALYZER_BIN" -c error_only.conf -f error_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/eo.txt" -O "$RESULTS_DIR/eo.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/eo.txt"
ERROR_TOTAL=$(extract_level_col "$OUT" "ERROR" 3)
ERROR_KW=$(extract_level_col "$OUT" "ERROR" 4)
FAIL_KW=$(extract_level_col "$OUT" "ERROR" 5)
TIMEOUT_KW=$(extract_level_col "$OUT" "ERROR" 6)

assert_numeric_eq "ERROR.error count = 8.0"   "8.0" "$ERROR_KW"
assert_numeric_eq "ERROR.fail count = 8.0"    "8.0" "$FAIL_KW"
assert_numeric_eq "ERROR.timeout count = 8.0" "8.0" "$TIMEOUT_KW"
assert_numeric_eq "ERROR weighted total = 24.0" "24.0" "$ERROR_TOTAL"

# ── 2.3 Bilinen skor testi (deterministik) ───────────────────────────────────
log_subsection "2.3 Known-score deterministic check"

# known_score.log: 4 satır, her biri tam 1 'error' + 1 'fail'
# Weights: ERROR=4, WARN=3, INFO=2, DEBUG=1
# ERROR weight=4 → 1*4+1*4=8 (ERROR satır toplamı: error + fail)
# WARN weight=3  → 1*3+1*3=6
# INFO weight=2  → 1*2+1*2=4
# DEBUG weight=1 → 1*1+1*1=2
# Total: 8+6+4+2 = 20

timeout 20 "$ANALYZER_BIN" -c known_score.conf -f known_score_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/known.txt" -O "$RESULTS_DIR/known.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/known.txt"
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")
ERROR_W=$(extract_level_col "$OUT" "ERROR" 3)
WARN_W=$(extract_level_col "$OUT" "WARN" 3)
INFO_W=$(extract_level_col "$OUT" "INFO" 3)
DEBUG_W=$(extract_level_col "$OUT" "DEBUG" 3)

assert_numeric_eq "TOTAL_WEIGHTED = 20.0"  "20.0" "$TOTAL"
assert_numeric_eq "ERROR weighted = 8.0"   "8.0"  "$ERROR_W"
assert_numeric_eq "WARN weighted = 6.0"    "6.0"  "$WARN_W"
assert_numeric_eq "INFO weighted = 4.0"    "4.0"  "$INFO_W"
assert_numeric_eq "DEBUG weighted = 2.0"   "2.0"  "$DEBUG_W"

# ── 2.4 No-match keywords (skor 0) ───────────────────────────────────────────
log_subsection "2.4 Keywords with no matches"

timeout 20 "$ANALYZER_BIN" -c known_score.conf -f known_score_priority.txt -k "xyzqwerty,nonexistent" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/nomatch.txt" -O "$RESULTS_DIR/nomatch.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/nomatch.txt"
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")
assert_numeric_eq "no-match keywords: total = 0.0" "0.0" "$TOTAL"

# ── 2.5 Long line testi ─────────────────────────────────────────────────────
log_subsection "2.5 Long lines (10 error/line × 49 lines × weight 4)"

timeout 20 "$ANALYZER_BIN" -c long_lines.conf -f empty_priority.txt -k "error" \
    -t 2 -w 2 -a 64 -b 64 -d 8 -T 5 \
    -o "$RESULTS_DIR/long.txt" -O "$RESULTS_DIR/long.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "long lines: exit 0" "0" "$EXIT"

OUT="$RESULTS_DIR/long.txt"
ERROR_KW=$(extract_level_col "$OUT" "ERROR" 4)
# 49 satır × 10 'error'/satır × weight 4 = 1960 ('error fail timeout ' × 10 = 10 error)
assert_numeric_eq "long lines: ERROR.error = 1960.0" "1960.0" "$ERROR_KW"

# ── 2.6 Counting determinism (3 ardışık run aynı sonuç vermeli) ──────────────
log_subsection "2.6 Counting is deterministic across runs"

SCORE1=""
SCORE2=""
SCORE3=""

for i in 1 2 3; do
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail,timeout" \
        -t 4 -w 4 -a 16 -b 16 -d 8 -T 5 \
        -o "$RESULTS_DIR/det_$i.txt" -O "$RESULTS_DIR/det_$i.bin" \
        > /dev/null 2>&1
done

S1=$(extract_field "$RESULTS_DIR/det_1.txt" "TOTAL_WEIGHTED_SCORE")
S2=$(extract_field "$RESULTS_DIR/det_2.txt" "TOTAL_WEIGHTED_SCORE")
S3=$(extract_field "$RESULTS_DIR/det_3.txt" "TOTAL_WEIGHTED_SCORE")

assert_eq "deterministic: run1 == run2" "$S1" "$S2"
assert_eq "deterministic: run2 == run3" "$S2" "$S3"

print_summary
