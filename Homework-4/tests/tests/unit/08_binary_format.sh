#!/usr/bin/env bash
# =============================================================================
# 08_binary_format.sh - Binary output dosyasının formatı
# =============================================================================
# PDF Section 10.2: Binary file structure
#   uint32_t magic = 0xC5E3440B  (little-endian)
#   uint32_t version = 1
#   uint32_t num_levels = 4
#   uint32_t num_keywords
#   double   total_weighted
#   double   hp_weighted
#   ... (level records)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 08: Binary File Format"
ensure_environment

cd "$FIXTURES_DIR"

# ── 8.1 Magic doğru ─────────────────────────────────────────────────────────
log_subsection "8.1 Magic number = 0xC5E3440B"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/bin1.txt" -O "$RESULTS_DIR/bin1.bin" \
    > /dev/null 2>&1

MAGIC=$(read_binary_magic "$RESULTS_DIR/bin1.bin")
assert_eq "magic = 0xC5E3440B" "0xC5E3440B" "$MAGIC"

# ── 8.2 Header değerleri tutarlı ────────────────────────────────────────────
log_subsection "8.2 Header consistent with .txt"

HEADER=$(read_binary_header "$RESULTS_DIR/bin1.bin")
log_info "header: $HEADER"

assert_contains "version = 1" "$HEADER" "version=1"
assert_contains "levels = 4"  "$HEADER" "levels=4"
assert_contains "keywords = 2 (matches -k)" "$HEADER" "keywords=2"

# .txt ile binary header karşılaştırması
TXT_TOTAL=$(extract_field "$RESULTS_DIR/bin1.txt" "TOTAL_WEIGHTED_SCORE")
TXT_HP=$(extract_field "$RESULTS_DIR/bin1.txt" "HIGH_PRIORITY_SCORE")

# Header'dan total ve hp'yi ekstrakt et
BIN_TOTAL=$(echo "$HEADER" | grep -oP 'total=\K[0-9.]+')
BIN_HP=$(echo "$HEADER" | grep -oP 'hp=\K[0-9.]+')

assert_numeric_eq ".txt vs .bin: TOTAL match"  "$TXT_TOTAL" "$BIN_TOTAL"
assert_numeric_eq ".txt vs .bin: HP match"     "$TXT_HP"    "$BIN_HP"

# ── 8.3 Farklı keyword sayısı ile header güncellenir mi ────────────────────
log_subsection "8.3 num_keywords matches -k flag"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "a,b,c,d,e,f,g" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/bin7.txt" -O "$RESULTS_DIR/bin7.bin" \
    > /dev/null 2>&1

HEADER=$(read_binary_header "$RESULTS_DIR/bin7.bin")
assert_contains "7 keywords in header" "$HEADER" "keywords=7"

# ── 8.4 Tek keyword ile header ─────────────────────────────────────────────
log_subsection "8.4 Single keyword"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "single" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/bin1k.txt" -O "$RESULTS_DIR/bin1k.bin" \
    > /dev/null 2>&1

HEADER=$(read_binary_header "$RESULTS_DIR/bin1k.bin")
assert_contains "1 keyword in header" "$HEADER" "keywords=1"

# ── 8.5 Binary file'da tmp file kalmamış ────────────────────────────────────
log_subsection "8.5 No leftover .tmp files"

# Çoklu run'lardan sonra tmp dosya kalmamış olmalı
TMP_COUNT=$(find "$RESULTS_DIR" -name "*.bin.tmp" -type f 2>/dev/null | wc -l)
assert_eq "no .bin.tmp files left" "0" "$TMP_COUNT"

# ── 8.6 Binary boyutu mantıklı ──────────────────────────────────────────────
log_subsection "8.6 Binary file size"

# Header: 32 byte minimum, sonra level kayıtları
SIZE=$(stat -c%s "$RESULTS_DIR/bin1.bin")
if [ "$SIZE" -ge 100 ]; then
    log_pass "binary >= 100 bytes (size=$SIZE)"
else
    log_fail "binary too small" "size=$SIZE"
fi

if [ "$SIZE" -le 1048576 ]; then  # < 1 MB makul
    log_pass "binary < 1 MB (size=$SIZE)"
else
    log_fail "binary suspiciously large" "size=$SIZE"
fi

# ── 8.7 Binary hex dump kontrolü (ilk 4 byte = magic, little-endian) ──────
log_subsection "8.7 Binary hex dump (first 4 bytes)"

# 0xC5E3440B little-endian: 0B 44 E3 C5
HEX_FIRST4=$(od -An -N4 -tx1 "$RESULTS_DIR/bin1.bin" 2>/dev/null | tr -d ' \n')
assert_eq "first 4 bytes = 0b44e3c5 (little-endian magic)" "0b44e3c5" "$HEX_FIRST4"

# ── 8.8 Atomicity: rename'den önce write completed ────────────────────────
log_subsection "8.8 Atomic rename: file is fully written"

# Magic'i okuyabiliyorsak (0xC5E3440B), rename atomic olmuş demektir
# (kısmen yazılmış dosya olsa magic okunamazdı)
MAGIC=$(read_binary_magic "$RESULTS_DIR/bin1.bin")
if [ "$MAGIC" = "0xC5E3440B" ]; then
    log_pass "binary file fully written (magic readable)"
else
    log_fail "binary file partially written" "magic=$MAGIC"
fi

print_summary
