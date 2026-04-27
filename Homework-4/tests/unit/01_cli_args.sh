#!/usr/bin/env bash
# =============================================================================
# 01_cli_args.sh - Komut satırı argümanlarının doğrulanması
# =============================================================================
# Bu testler, analyzer'ın CLI argümanlarını doğru parse ettiğini ve geçersiz
# parametreleri (eksik, negatif, format hatası) reddettiğini doğrular.
# PDF Section 11 (CLI Specification) referans alınmıştır.
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 01: CLI Argument Validation"
ensure_environment

cd "$FIXTURES_DIR"

# ── 1.1 Hiç argüman olmadan çalıştır → exit code != 0 ────────────────────────
log_subsection "1.1 No arguments"
timeout 20 "$ANALYZER_BIN" > "$RESULTS_DIR/cli_no_args.out" 2> "$RESULTS_DIR/cli_no_args.err"
EXIT=$?
assert_neq "exit code is not 0 for missing args" "0" "$EXIT"

# ── 1.2 Eksik -c ile çalıştır ────────────────────────────────────────────────
log_subsection "1.2 Missing required flag (-c)"
timeout 20 "$ANALYZER_BIN" -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_no_c.txt" -O "$RESULTS_DIR/cli_no_c.bin" \
    > /dev/null 2> "$RESULTS_DIR/cli_no_c.err"
EXIT=$?
assert_neq "exit != 0 when -c is missing" "0" "$EXIT"

# ── 1.3 Geçersiz dosya yolu ──────────────────────────────────────────────────
log_subsection "1.3 Non-existent config file"
echo "/nonexistent/path/foo.log" > "$RESULTS_DIR/bad.conf"
timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/bad.conf" -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_bad_file.txt" -O "$RESULTS_DIR/cli_bad_file.bin" \
    > "$RESULTS_DIR/cli_bad_file.out" 2> "$RESULTS_DIR/cli_bad_file.err"
EXIT=$?
# Program ya hata verir ya da boş sonuç döner - en azından çökmemeli (segfault)
if [ "$EXIT" -eq 139 ] || [ "$EXIT" -eq 11 ]; then
    log_fail "non-existent file: no segfault" "exit=$EXIT (segfault)"
else
    log_pass "non-existent file: no segfault"
fi

# ── 1.4 Negatif/sıfır thread sayısı ──────────────────────────────────────────
log_subsection "1.4 Invalid thread count (0)"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 0 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_t0.txt" -O "$RESULTS_DIR/cli_t0.bin" \
    > /dev/null 2> "$RESULTS_DIR/cli_t0.err"
EXIT=$?
assert_neq "exit != 0 for -t 0" "0" "$EXIT"

# ── 1.5 Negatif worker sayısı ────────────────────────────────────────────────
log_subsection "1.5 Invalid worker count (0)"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 0 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_w0.txt" -O "$RESULTS_DIR/cli_w0.bin" \
    > /dev/null 2> "$RESULTS_DIR/cli_w0.err"
EXIT=$?
assert_neq "exit != 0 for -w 0" "0" "$EXIT"

# ── 1.6 Sıfır buffer sayısı ──────────────────────────────────────────────────
log_subsection "1.6 Invalid buffer size (-a 0)"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 0 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_a0.txt" -O "$RESULTS_DIR/cli_a0.bin" \
    > /dev/null 2> "$RESULTS_DIR/cli_a0.err"
EXIT=$?
assert_neq "exit != 0 for -a 0" "0" "$EXIT"

# ── 1.7 Boş keyword listesi ──────────────────────────────────────────────────
log_subsection "1.7 Empty keyword list"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/cli_kemp.txt" -O "$RESULTS_DIR/cli_kemp.bin" \
    > /dev/null 2> "$RESULTS_DIR/cli_kemp.err"
EXIT=$?
# Boş keyword için ya hata vermeli ya da temiz çalışmalı
if [ "$EXIT" -eq 139 ] || [ "$EXIT" -eq 11 ]; then
    log_fail "empty keyword list: no segfault" "exit=$EXIT"
else
    log_pass "empty keyword list: no segfault"
fi

# ── 1.8 Mantıklı argümanlar ile başarılı çalışma ─────────────────────────────
log_subsection "1.8 Valid arguments accepted"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/cli_valid.txt" -O "$RESULTS_DIR/cli_valid.bin" \
    > "$RESULTS_DIR/cli_valid.out" 2> "$RESULTS_DIR/cli_valid.err"
EXIT=$?
assert_exit_code "valid run exits 0" "0" "$EXIT"
assert_file_exists "valid run creates .txt output" "$RESULTS_DIR/cli_valid.txt"
assert_file_exists "valid run creates .bin output" "$RESULTS_DIR/cli_valid.bin"

# ── 1.9 Çoklu keyword (3 keyword) ───────────────────────────────────────────
log_subsection "1.9 Multiple keywords parsing"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail,timeout" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/cli_multi.txt" -O "$RESULTS_DIR/cli_multi.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "multi-keyword run exits 0" "0" "$EXIT"
KW=$(extract_field "$RESULTS_DIR/cli_multi.txt" "KEYWORD_LIST")
assert_eq "keyword list preserved in output" "error,fail,timeout" "$KW"

# ── 1.10 Output dosyaları yazılabilir mi ────────────────────────────────────
log_subsection "1.10 Output file writability"
mkdir -p "$RESULTS_DIR/subdir"
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/subdir/out.txt" -O "$RESULTS_DIR/subdir/out.bin" \
    > /dev/null 2>&1
EXIT=$?
assert_exit_code "subdir output exit 0" "0" "$EXIT"
assert_file_exists "subdir .txt output" "$RESULTS_DIR/subdir/out.txt"
assert_file_exists "subdir .bin output" "$RESULTS_DIR/subdir/out.bin"

# ── Özet ─────────────────────────────────────────────────────────────────────
print_summary
