#!/usr/bin/env bash
# =============================================================================
# 03_parser_robustness.sh - Parser malformed input handling
# =============================================================================
# Bu testler PDF Section 8'in parser robustness gereksinimlerini doğrular:
# - Geçersiz formatlı satırlar atlanmalı (program çökmemeli)
# - Boş satırlar göz ardı edilmeli
# - Bilinmeyen seviyeler (BADLEVEL) atlanmalı
# - Tamamen boş dosyalar program çıktısı vermeli (toplam 0)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 03: Parser Robustness"
ensure_environment

cd "$FIXTURES_DIR"

# ── 3.1 Boş dosya işleme ────────────────────────────────────────────────────
log_subsection "3.1 Empty log file"

timeout 20 "$ANALYZER_BIN" -c empty.conf -f empty_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/empty.txt" -O "$RESULTS_DIR/empty.bin" \
    > "$RESULTS_DIR/empty.out" 2>&1
EXIT=$?
assert_exit_code "empty file: exit 0" "0" "$EXIT"
assert_file_exists "empty file: .txt created" "$RESULTS_DIR/empty.txt"
TOTAL=$(extract_field "$RESULTS_DIR/empty.txt" "TOTAL_WEIGHTED_SCORE")
assert_numeric_eq "empty file: total = 0.0" "0.0" "$TOTAL"

# ── 3.2 Tamamı malformed log ─────────────────────────────────────────────────
log_subsection "3.2 All lines malformed"

timeout 20 "$ANALYZER_BIN" -c malformed.conf -f empty_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/malf.txt" -O "$RESULTS_DIR/malf.bin" \
    > "$RESULTS_DIR/malf.out" 2>&1
EXIT=$?
assert_exit_code "all malformed: exit 0 (no crash)" "0" "$EXIT"
TOTAL=$(extract_field "$RESULTS_DIR/malf.txt" "TOTAL_WEIGHTED_SCORE")
assert_numeric_eq "all malformed: total = 0.0" "0.0" "$TOTAL"

# ── 3.3 Karışık (geçerli + malformed) ───────────────────────────────────────
log_subsection "3.3 Mixed valid + malformed lines"

# simple_nginx.log içinde 1 boş satır + 1 BADLEVEL satırı + 6 geçerli satır var
# Toplam Reader satırı: 7 (geçerli)
# simple_kernel.log: 7 satır
# Toplam: 13 entries

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/mix.txt" -O "$RESULTS_DIR/mix.bin" \
    > "$RESULTS_DIR/mix.out" 2>&1

# Toplam entry sayısı: 4+3+3+3 = 13
ERROR_E=$(extract_level_col "$RESULTS_DIR/mix.txt" "ERROR" 2)
WARN_E=$(extract_level_col "$RESULTS_DIR/mix.txt" "WARN" 2)
INFO_E=$(extract_level_col "$RESULTS_DIR/mix.txt" "INFO" 2)
DEBUG_E=$(extract_level_col "$RESULTS_DIR/mix.txt" "DEBUG" 2)
TOTAL_E=$((ERROR_E + WARN_E + INFO_E + DEBUG_E))

assert_eq "mixed: total entries = 13" "13" "$TOTAL_E"
assert_eq "mixed: ERROR count = 4" "4" "$ERROR_E"
assert_eq "mixed: WARN count = 3" "3" "$WARN_E"
assert_eq "mixed: INFO count = 3" "3" "$INFO_E"
assert_eq "mixed: DEBUG count = 3" "3" "$DEBUG_E"

# Malformed satır SİSTEM_SUMMARY içinde sayılmamalı (BADLEVEL)
# stdout'taki "malformed=N" sayısını kontrol et
MALFORMED_LINES=$(grep -oP 'malformed=\K[0-9]+' "$RESULTS_DIR/mix.out" | \
                  awk '{s+=$1} END {print s+0}')
# nginx'teki BADLEVEL + boş satır = 1 (boş satır skip edilmeli, BADLEVEL malformed)
# Reader'lar farklı thread olduğu için hangi thread'in işlediği değişebilir
if [ "$MALFORMED_LINES" -ge 1 ]; then
    log_pass "mixed: malformed line(s) detected (count=$MALFORMED_LINES)"
else
    log_fail "mixed: BADLEVEL line should be counted as malformed" "got $MALFORMED_LINES"
fi

# ── 3.4 Tek satır log (corner case) ─────────────────────────────────────────
log_subsection "3.4 Single-line log file"

timeout 20 "$ANALYZER_BIN" -c single.conf -f empty_priority.txt -k "error,one" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/sgl.txt" -O "$RESULTS_DIR/sgl.bin" \
    > "$RESULTS_DIR/sgl.out" 2>&1
EXIT=$?
assert_exit_code "single line: exit 0" "0" "$EXIT"

ERROR_E=$(extract_level_col "$RESULTS_DIR/sgl.txt" "ERROR" 2)
assert_eq "single line: ERROR = 1" "1" "$ERROR_E"

# 'one' = 0 (yok), 'error' = 1 (1 occurrence × weight 4)
ERROR_KW=$(extract_level_col "$RESULTS_DIR/sgl.txt" "ERROR" 4)
assert_numeric_eq "single line: ERROR.error = 4.0" "4.0" "$ERROR_KW"

# ── 3.5 Birden fazla thread ile boş dosya ──────────────────────────────────
log_subsection "3.5 Empty file with high thread count"

timeout 20 "$ANALYZER_BIN" -c empty.conf -f empty_priority.txt -k "error" \
    -t 8 -w 4 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/emp_t8.txt" -O "$RESULTS_DIR/emp_t8.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "empty + 8 threads: exit 0" "0" "$EXIT"

# Daha fazla thread olduğunda 0 entry doğru raporlanmalı
TOTAL=$(extract_field "$RESULTS_DIR/emp_t8.txt" "TOTAL_WEIGHTED_SCORE")
assert_numeric_eq "empty + 8 threads: total = 0.0" "0.0" "$TOTAL"

# ── 3.6 CRLF (Windows line endings) ─────────────────────────────────────────
log_subsection "3.6 CRLF line endings"

# Windows tarzı CRLF satır sonlarıyla bir log dosyası oluştur
printf '[2025-03-10 08:00:00] [ERROR] [src] error here\r\n[2025-03-10 08:00:01] [WARN] [src] another\r\n' \
    > "$RESULTS_DIR/crlf.log"
echo "$RESULTS_DIR/crlf.log" > "$RESULTS_DIR/crlf.conf"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/crlf.conf" -f empty_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/crlf.txt" -O "$RESULTS_DIR/crlf.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "CRLF input: exit 0" "0" "$EXIT"

ERROR_E=$(extract_level_col "$RESULTS_DIR/crlf.txt" "ERROR" 2)
WARN_E=$(extract_level_col "$RESULTS_DIR/crlf.txt" "WARN" 2)
assert_eq "CRLF: ERROR count = 1" "1" "$ERROR_E"
assert_eq "CRLF: WARN count = 1" "1" "$WARN_E"

# ── 3.7 Source field uzun isimle ────────────────────────────────────────────
log_subsection "3.7 Long source name"

LONG_SRC="thisisaverylongsourcename_with_underscores_and_more"
echo "[2025-03-10 08:00:00] [ERROR] [$LONG_SRC] error msg" > "$RESULTS_DIR/long_src.log"
echo "$RESULTS_DIR/long_src.log" > "$RESULTS_DIR/long_src.conf"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/long_src.conf" -f empty_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/long_src.txt" -O "$RESULTS_DIR/long_src.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "long source name: exit 0" "0" "$EXIT"
ERROR_E=$(extract_level_col "$RESULTS_DIR/long_src.txt" "ERROR" 2)
assert_eq "long source: parsed OK" "1" "$ERROR_E"

print_summary
