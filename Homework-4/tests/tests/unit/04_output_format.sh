#!/usr/bin/env bash
# =============================================================================
# 04_output_format.sh - Output dosyalarının PDF formatına uygunluğu
# =============================================================================
# PDF Section 10 (Output Specification) alanları:
# .txt:
#   KEYWORD_LIST:, FILES:, TOTAL_WEIGHTED_SCORE:, HIGH_PRIORITY_SCORE:,
#   FILTER_FILE:
#   # Levels sorted by total_weighted_score DESC
#   LEVEL  ENTRIES  WEIGHTED_SCORE  <kw1>  <kw2> ...
#   # Top-3 sources per level
#   <LEVEL>  src:N  src:N  src:N
#   # Per-thread contributions
#   <LEVEL>  thread_0:N  thread_1:N ...
# .bin:
#   magic = 0xC5E3440B (little-endian)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 04: Output Format Compliance"
ensure_environment

cd "$FIXTURES_DIR"

# Bir baseline run hazırla, tüm format testleri buna dayanacak
timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail,timeout" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/fmt.txt" -O "$RESULTS_DIR/fmt.bin" \
    > /dev/null 2>&1

OUT_TXT="$RESULTS_DIR/fmt.txt"
OUT_BIN="$RESULTS_DIR/fmt.bin"

# ── 4.1 Header alanlarının varlığı ──────────────────────────────────────────
log_subsection "4.1 Required header fields present"

assert_grep "KEYWORD_LIST present"          "^KEYWORD_LIST:"          "$OUT_TXT"
assert_grep "FILES present"                 "^FILES:"                 "$OUT_TXT"
assert_grep "TOTAL_WEIGHTED_SCORE present"  "^TOTAL_WEIGHTED_SCORE:"  "$OUT_TXT"
assert_grep "HIGH_PRIORITY_SCORE present"   "^HIGH_PRIORITY_SCORE:"   "$OUT_TXT"
assert_grep "FILTER_FILE present"           "^FILTER_FILE:"           "$OUT_TXT"

# ── 4.2 Section başlıkları (yorum satırları) ────────────────────────────────
log_subsection "4.2 Section headers present"

assert_grep "Levels section header"       "^# Levels sorted"       "$OUT_TXT"
assert_grep "Top-3 sources section"       "^# Top-3"               "$OUT_TXT"
assert_grep "Per-thread section"          "^# Per-thread"          "$OUT_TXT"

# ── 4.3 Tüm 4 LEVEL satırının varlığı ───────────────────────────────────────
log_subsection "4.3 All 4 level rows present"

assert_grep_count "ERROR satırı (1 levels + 1 top-3 + 1 per-thread)" "ERROR" "$OUT_TXT" 3
assert_grep_count "WARN satırı"  "WARN"  "$OUT_TXT" 3
assert_grep_count "INFO satırı"  "INFO"  "$OUT_TXT" 3
assert_grep_count "DEBUG satırı" "DEBUG" "$OUT_TXT" 3

# ── 4.4 Levels sıralaması: WEIGHTED_SCORE DESC ──────────────────────────────
log_subsection "4.4 Levels sorted DESC by weighted score"

# Level satırlarını ekstrakt et (LEVEL kolonu trim, weighted score kolon 3)
SORTED_OK=$(awk '
    /^# Levels sorted/ { p=1; next }
    /^# Top-3/        { p=0 }
    p && /^[ ]+(ERROR|WARN|INFO|DEBUG)[ ]/ {
        # 1. token = LEVEL, 3. token = weighted score
        if (prev != "" && $3+0 > prev+0) { print "BAD"; exit }
        prev = $3
    }
    END { if (prev != "") print "OK" }
' "$OUT_TXT")

if [ "$SORTED_OK" = "OK" ]; then
    log_pass "levels: weighted score DESC sıralı"
else
    log_fail "levels: weighted score DESC sıralı değil" "got '$SORTED_OK'"
fi

# ── 4.5 Top-3 source: maks 3 source per LEVEL ──────────────────────────────
log_subsection "4.5 Top-3 sources: at most 3 per level"

for lvl in ERROR WARN INFO DEBUG; do
    # Top-3 bölümündeki LEVEL satırını bul
    SRC_LINE=$(awk -v lvl="$lvl" '
        /^# Top-3/ { p=1; next }
        /^# Per-thread/ { p=0 }
        p && $1 == lvl { print; exit }
    ' "$OUT_TXT")

    # "<level> src1:n src2:n src3:n" formatında: NF maksimum 4 olmalı
    # Boş ise NF=2 ('LEVEL -' veya 'LEVEL') olabilir
    NF_VAL=$(echo "$SRC_LINE" | awk '{ print NF }')
    if [ "$NF_VAL" -le 4 ]; then
        log_pass "$lvl: top-3 source count <= 3 (NF=$NF_VAL)"
    else
        log_fail "$lvl: top-3 source count > 3" "NF=$NF_VAL line='$SRC_LINE'"
    fi
done

# ── 4.6 Per-thread satırı: thread_N:score formatı ───────────────────────────
log_subsection "4.6 Per-thread entry format"

# Beklenti: 'ERROR    thread_0:12.0  thread_1:4.0'
# Her thread_i:value şeklinde olmalı
THREAD_FORMAT_OK=$(awk '
    /^# Per-thread/ { p=1; next }
    p && /^[A-Z]/ {
        for (i=2; i<=NF; i++) {
            if ($i !~ /^thread_[0-9]+:[0-9]+\.[0-9]+$/) {
                print "BAD: " $i
                exit
            }
        }
    }
    END { print "OK" }
' "$OUT_TXT")

assert_eq "per-thread entries match thread_N:F.F" "OK" "$THREAD_FORMAT_OK"

# ── 4.7 Binary file: magic header ───────────────────────────────────────────
log_subsection "4.7 Binary file magic header"

MAGIC=$(read_binary_magic "$OUT_BIN")
assert_eq "binary magic = 0xC5E3440B" "0xC5E3440B" "$MAGIC"

# ── 4.8 Binary file: header doğruluğu ───────────────────────────────────────
log_subsection "4.8 Binary file header values"

HEADER=$(read_binary_header "$OUT_BIN")
assert_contains "binary version=1" "$HEADER" "version=1"
assert_contains "binary levels=4"  "$HEADER" "levels=4"
assert_contains "binary keywords=3" "$HEADER" "keywords=3"

# ── 4.9 Binary file: dosya boyutu makul mü ──────────────────────────────────
log_subsection "4.9 Binary file size sanity"

SIZE=$(stat -c%s "$OUT_BIN" 2>/dev/null || stat -f%z "$OUT_BIN" 2>/dev/null)
# Header: 32 byte + 4 levels × variable size
# En az 100 byte olmalı (header + 4 level)
if [ "$SIZE" -ge 100 ]; then
    log_pass "binary size makul (size=$SIZE)"
else
    log_fail "binary size çok küçük" "size=$SIZE"
fi

# ── 4.10 Çıktı dosyaları stat: regular file mi (symlink değil) ──────────────
log_subsection "4.10 Output files are regular (atomic rename test)"

if [ -L "$OUT_TXT" ]; then
    log_fail ".txt is a symlink (atomic rename failed)" "$OUT_TXT"
else
    log_pass ".txt is a regular file"
fi

if [ -L "$OUT_BIN" ]; then
    log_fail ".bin is a symlink (atomic rename failed)" "$OUT_BIN"
else
    log_pass ".bin is a regular file"
fi

# .bin.tmp dosyası kalmamış olmalı
if [ -e "${OUT_BIN}.tmp" ]; then
    log_fail ".bin.tmp left behind (rename did not complete)" "${OUT_BIN}.tmp exists"
else
    log_pass "no .bin.tmp leftover (atomic rename clean)"
fi

# ── 4.11 KEYWORD_LIST içeriği doğru ─────────────────────────────────────────
log_subsection "4.11 KEYWORD_LIST content"

KW=$(extract_field "$OUT_TXT" "KEYWORD_LIST")
assert_eq "KEYWORD_LIST = 'error,fail,timeout'" "error,fail,timeout" "$KW"

# ── 4.12 FILES alanı doğru ──────────────────────────────────────────────────
log_subsection "4.12 FILES count"

FILES=$(extract_field "$OUT_TXT" "FILES")
assert_eq "FILES = 2" "2" "$FILES"

# ── 4.13 FILTER_FILE alanı doğru ────────────────────────────────────────────
log_subsection "4.13 FILTER_FILE name"

FF=$(extract_field "$OUT_TXT" "FILTER_FILE")
# Tam isim ya da göreceli yol olabilir
assert_contains "FILTER_FILE içeriyor 'priority.txt'" "$FF" "priority.txt"

# ── 4.14 LEVEL header satırının varlığı ─────────────────────────────────────
log_subsection "4.14 Column header line"

assert_grep "column header (LEVEL ENTRIES WEIGHTED_SCORE)" \
    "LEVEL.*ENTRIES.*WEIGHTED_SCORE" "$OUT_TXT"

# Keyword isimleri header satırında olmalı
assert_grep "header: 'error' column" "LEVEL.*error" "$OUT_TXT"
assert_grep "header: 'fail' column"  "error.*fail"  "$OUT_TXT"

print_summary
