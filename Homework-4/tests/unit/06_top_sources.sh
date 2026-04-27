#!/usr/bin/env bash
# =============================================================================
# 06_top_sources.sh - Top-3 source seçimi ve sıralaması
# =============================================================================
# PDF Section 10: "Top-3 sources per level, sorted by hit count DESC"
# multi_source.log fixture: alpha:3, beta:2, gamma:2, delta:1
# Beklenen: ERROR    alpha:3  beta:2  gamma:2 (delta dışlanır)
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 06: Top-3 Source Selection"
ensure_environment

cd "$FIXTURES_DIR"

# ── 6.1 Multi-source: doğru top-3 ───────────────────────────────────────────
log_subsection "6.1 Top-3 from 4 sources (alpha:3, beta:2, gamma:2, delta:1)"

timeout 20 "$ANALYZER_BIN" -c multi_source.conf -f multi_source_priority.txt -k "msg" \
    -t 1 -w 1 -a 16 -b 16 -d 4 -T 5 \
    -o "$RESULTS_DIR/ts.txt" -O "$RESULTS_DIR/ts.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/ts.txt"

ALPHA=$(extract_top_source_count "$OUT" "ERROR" "alpha")
BETA=$(extract_top_source_count "$OUT" "ERROR" "beta")
GAMMA=$(extract_top_source_count "$OUT" "ERROR" "gamma")
DELTA=$(extract_top_source_count "$OUT" "ERROR" "delta")

assert_eq "ERROR: alpha hits = 3" "3" "$ALPHA"
assert_eq "ERROR: beta hits = 2"  "2" "$BETA"
assert_eq "ERROR: gamma hits = 2" "2" "$GAMMA"

# delta çıktıda olmamalı (top-3 dışında)
if [ -z "$DELTA" ] || [ "$DELTA" = "0" ]; then
    log_pass "ERROR: delta NOT in top-3 (correct - only 1 hit)"
else
    log_fail "ERROR: delta in top-3 unexpectedly" "delta=$DELTA"
fi

# ── 6.2 Top-3 sıralı (DESC): alpha önce gelmeli ─────────────────────────────
log_subsection "6.2 Top-3 sorted by hits DESC"

# 'ERROR    alpha:3  beta:2  gamma:2' satırını al
TOP_LINE=$(awk '
    /^# Top-3/ { p=1; next }
    /^# Per-thread/ { p=0 }
    p && $1 == "ERROR" { print; exit }
' "$OUT")

# İlk source 'alpha' olmalı (en fazla hit)
FIRST_SRC=$(echo "$TOP_LINE" | awk '{ split($2, a, ":"); print a[1] }')
assert_eq "first source = alpha (highest hits)" "alpha" "$FIRST_SRC"

# Sıralama doğrulaması: hit count'lar non-increasing olmalı
SORT_OK=$(echo "$TOP_LINE" | awk '
    {
        prev = 1e9
        for (i=2; i<=NF; i++) {
            split($i, a, ":")
            cur = a[2] + 0
            if (cur > prev) { print "BAD"; exit }
            prev = cur
        }
        print "OK"
    }
')
assert_eq "top-3 hits non-increasing" "OK" "$SORT_OK"

# ── 6.3 Az source'lu durum: 1 source ────────────────────────────────────────
log_subsection "6.3 Single source: only one entry in top-3"

cat > "$RESULTS_DIR/one_src.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [onlysrc] entry1
[2025-03-10 08:00:01] [ERROR] [onlysrc] entry2
[2025-03-10 08:00:02] [ERROR] [onlysrc] entry3
EOF
echo "$RESULTS_DIR/one_src.log" > "$RESULTS_DIR/one_src.conf"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/one_src.conf" -f empty_priority.txt -k "entry" \
    -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/one_src.txt" -O "$RESULTS_DIR/one_src.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/one_src.txt"
COUNT=$(extract_top_source_count "$OUT" "ERROR" "onlysrc")
assert_eq "single source: onlysrc:3 in top-3" "3" "$COUNT"

# ── 6.4 Tam 3 source (boundary case) ────────────────────────────────────────
log_subsection "6.4 Exactly 3 sources (boundary case)"

cat > "$RESULTS_DIR/three_src.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [s1] msg
[2025-03-10 08:00:01] [ERROR] [s2] msg
[2025-03-10 08:00:02] [ERROR] [s3] msg
[2025-03-10 08:00:03] [ERROR] [s1] msg
[2025-03-10 08:00:04] [ERROR] [s2] msg
[2025-03-10 08:00:05] [ERROR] [s1] msg
EOF
echo "$RESULTS_DIR/three_src.log" > "$RESULTS_DIR/three_src.conf"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/three_src.conf" -f empty_priority.txt -k "msg" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/three_src.txt" -O "$RESULTS_DIR/three_src.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/three_src.txt"
S1=$(extract_top_source_count "$OUT" "ERROR" "s1")
S2=$(extract_top_source_count "$OUT" "ERROR" "s2")
S3=$(extract_top_source_count "$OUT" "ERROR" "s3")

assert_eq "three sources: s1:3" "3" "$S1"
assert_eq "three sources: s2:2" "2" "$S2"
assert_eq "three sources: s3:1" "1" "$S3"

# Top-3 line: 3 source bekliyoruz
TOP_LINE=$(awk '
    /^# Top-3/ { p=1; next }
    /^# Per-thread/ { p=0 }
    p && $1 == "ERROR" { print; exit }
' "$OUT")
NF_VAL=$(echo "$TOP_LINE" | awk '{ print NF }')
assert_eq "exactly 3 sources: NF = 4 (LEVEL + 3 src)" "4" "$NF_VAL"

# ── 6.5 Çok source (>3): sadece top-3 görünmeli ─────────────────────────────
log_subsection "6.5 Many sources (5): only top-3 displayed"

cat > "$RESULTS_DIR/many_src.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [a] m
[2025-03-10 08:00:01] [ERROR] [a] m
[2025-03-10 08:00:02] [ERROR] [a] m
[2025-03-10 08:00:03] [ERROR] [a] m
[2025-03-10 08:00:04] [ERROR] [a] m
[2025-03-10 08:00:05] [ERROR] [b] m
[2025-03-10 08:00:06] [ERROR] [b] m
[2025-03-10 08:00:07] [ERROR] [b] m
[2025-03-10 08:00:08] [ERROR] [b] m
[2025-03-10 08:00:09] [ERROR] [c] m
[2025-03-10 08:00:10] [ERROR] [c] m
[2025-03-10 08:00:11] [ERROR] [c] m
[2025-03-10 08:00:12] [ERROR] [d] m
[2025-03-10 08:00:13] [ERROR] [d] m
[2025-03-10 08:00:14] [ERROR] [e] m
EOF
echo "$RESULTS_DIR/many_src.log" > "$RESULTS_DIR/many_src.conf"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/many_src.conf" -f empty_priority.txt -k "m" \
    -t 1 -w 1 -a 16 -b 16 -d 4 -T 5 \
    -o "$RESULTS_DIR/many.txt" -O "$RESULTS_DIR/many.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/many.txt"
TOP_LINE=$(awk '
    /^# Top-3/ { p=1; next }
    /^# Per-thread/ { p=0 }
    p && $1 == "ERROR" { print; exit }
' "$OUT")
NF_VAL=$(echo "$TOP_LINE" | awk '{ print NF }')
assert_eq "many sources: top-3 limited to 3 (NF=4)" "4" "$NF_VAL"

# Top-3 olmayan kaynaklar (d, e) listede olmamalı
A=$(extract_top_source_count "$OUT" "ERROR" "a")
B=$(extract_top_source_count "$OUT" "ERROR" "b")
C=$(extract_top_source_count "$OUT" "ERROR" "c")
D=$(extract_top_source_count "$OUT" "ERROR" "d")
E=$(extract_top_source_count "$OUT" "ERROR" "e")

assert_eq "many: a:5 in top-3" "5" "$A"
assert_eq "many: b:4 in top-3" "4" "$B"
assert_eq "many: c:3 in top-3" "3" "$C"

if [ -z "$D" ]; then
    log_pass "many: d NOT in top-3 (correct)"
else
    log_fail "many: d should not appear" "got $D"
fi
if [ -z "$E" ]; then
    log_pass "many: e NOT in top-3 (correct)"
else
    log_fail "many: e should not appear" "got $E"
fi

print_summary
