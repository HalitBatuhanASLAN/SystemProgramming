#!/usr/bin/env bash
# =============================================================================
# 07_priority_filter.sh - HIGH_PRIORITY_SCORE hesaplaması
# =============================================================================
# PDF Section 8: priority.txt içindeki source'lardan gelen entry'lerin
# weighted score'ları HIGH_PRIORITY_SCORE'a eklenir.
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Unit Test 07: High Priority Filter"
ensure_environment

cd "$FIXTURES_DIR"

# ── 7.1 Boş priority.txt → HIGH_PRIORITY_SCORE = 0 ──────────────────────────
log_subsection "7.1 Empty priority file → HP score = 0"

timeout 20 "$ANALYZER_BIN" -c simple.conf -f empty_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/hp_empty.txt" -O "$RESULTS_DIR/hp_empty.bin" \
    > /dev/null 2>&1

HP=$(extract_field "$RESULTS_DIR/hp_empty.txt" "HIGH_PRIORITY_SCORE")
assert_numeric_eq "empty priority: HP = 0.0" "0.0" "$HP"

# Total score yine de hesaplanmış olmalı
TOTAL=$(extract_field "$RESULTS_DIR/hp_empty.txt" "TOTAL_WEIGHTED_SCORE")
if awk -v t="$TOTAL" 'BEGIN { exit !(t > 0) }'; then
    log_pass "empty priority: total > 0 (still computed)"
else
    log_fail "empty priority: total should still be > 0" "got $TOTAL"
fi

# ── 7.2 Tek source priority: kernel ─────────────────────────────────────────
log_subsection "7.2 Priority = {kernel}: only kernel entries count"

# simple_priority.txt: kernel + auth
# simple_kernel.log'da kernel:3, simple_nginx.log'da nginx:6, auth:0
# (auth entry simple_kernel.log'ta 1 tane var)
# kernel + auth entry'lerin weighted score'u HIGH_PRIORITY_SCORE'a gelmeli

timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 5 \
    -o "$RESULTS_DIR/hp_kr.txt" -O "$RESULTS_DIR/hp_kr.bin" \
    > /dev/null 2>&1

HP=$(extract_field "$RESULTS_DIR/hp_kr.txt" "HIGH_PRIORITY_SCORE")
TOTAL=$(extract_field "$RESULTS_DIR/hp_kr.txt" "TOTAL_WEIGHTED_SCORE")

# HP > 0 olmalı (kernel'de error var)
if awk -v hp="$HP" 'BEGIN { exit !(hp > 0) }'; then
    log_pass "priority kernel+auth: HP > 0 (HP=$HP)"
else
    log_fail "priority kernel+auth: HP should be > 0" "got $HP"
fi

# HP <= TOTAL (subset olduğu için)
if awk -v hp="$HP" -v t="$TOTAL" 'BEGIN { exit !(hp <= t) }'; then
    log_pass "priority kernel+auth: HP <= TOTAL ($HP <= $TOTAL)"
else
    log_fail "priority kernel+auth: HP > TOTAL!" "HP=$HP TOTAL=$TOTAL"
fi

# ── 7.3 Kontrollü test: bilinen HP skoru ────────────────────────────────────
log_subsection "7.3 Controlled test: known HP score"

# 4 satır, 2'si 'src1' (priority), 2'si 'src2' (non-priority)
# Her satır 1 'error', her seviye:
# ERROR weight=4, WARN weight=2 → 1*4=4 each; 1*2=2 each
cat > "$RESULTS_DIR/hp_ctrl.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [src1] error
[2025-03-10 08:00:01] [ERROR] [src2] error
[2025-03-10 08:00:02] [WARN] [src1] error
[2025-03-10 08:00:03] [WARN] [src2] error
EOF
echo "$RESULTS_DIR/hp_ctrl.log" > "$RESULTS_DIR/hp_ctrl.conf"
echo "src1" > "$RESULTS_DIR/hp_ctrl_priority.txt"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/hp_ctrl.conf" -f "$RESULTS_DIR/hp_ctrl_priority.txt" \
    -k "error" -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/hp_ctrl.txt" -O "$RESULTS_DIR/hp_ctrl.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/hp_ctrl.txt"
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")
HP=$(extract_field "$OUT" "HIGH_PRIORITY_SCORE")

# Beklenen (weights ERROR=4, WARN=3):
# src1 ERROR: 1 error × 4 = 4
# src2 ERROR: 1 error × 4 = 4
# src1 WARN: 1 error × 3 = 3
# src2 WARN: 1 error × 3 = 3
# TOTAL = 14
# HP (only src1) = 4 + 3 = 7

assert_numeric_eq "controlled: TOTAL = 14.0" "14.0" "$TOTAL"
assert_numeric_eq "controlled: HP (src1 only) = 7.0" "7.0" "$HP"

# ── 7.4 İki priority source ─────────────────────────────────────────────────
log_subsection "7.4 Two priority sources: HP = TOTAL"

echo -e "src1\nsrc2" > "$RESULTS_DIR/hp_both.txt"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/hp_ctrl.conf" -f "$RESULTS_DIR/hp_both.txt" \
    -k "error" -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/hp_both_out.txt" -O "$RESULTS_DIR/hp_both_out.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/hp_both_out.txt"
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")
HP=$(extract_field "$OUT" "HIGH_PRIORITY_SCORE")

assert_numeric_eq "all-in-priority: HP == TOTAL ($TOTAL)" "$TOTAL" "$HP"

# ── 7.5 Eşleşmeyen priority (her zaman 0) ───────────────────────────────────
log_subsection "7.5 Priority with no matching source"

echo "nonexistent_src" > "$RESULTS_DIR/hp_none.txt"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/hp_ctrl.conf" -f "$RESULTS_DIR/hp_none.txt" \
    -k "error" -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/hp_none_out.txt" -O "$RESULTS_DIR/hp_none_out.bin" \
    > /dev/null 2>&1

OUT="$RESULTS_DIR/hp_none_out.txt"
HP=$(extract_field "$OUT" "HIGH_PRIORITY_SCORE")
TOTAL=$(extract_field "$OUT" "TOTAL_WEIGHTED_SCORE")

assert_numeric_eq "no-match priority: HP = 0.0" "0.0" "$HP"
# TOTAL hala olmalı
if awk -v t="$TOTAL" 'BEGIN { exit !(t > 0) }'; then
    log_pass "no-match priority: TOTAL preserved (TOTAL=$TOTAL)"
else
    log_fail "no-match priority: TOTAL should be > 0" "got $TOTAL"
fi

# ── 7.6 Priority filter etkisi (Dispatcher log'u) ──────────────────────────
log_subsection "7.6 Dispatcher logs 'High-priority: YES/NO'"

timeout 20 "$ANALYZER_BIN" -c "$RESULTS_DIR/hp_ctrl.conf" -f "$RESULTS_DIR/hp_ctrl_priority.txt" \
    -k "error" -t 1 -w 1 -a 4 -b 4 -d 2 -T 5 \
    -o "$RESULTS_DIR/hp_ctrl2.txt" -O "$RESULTS_DIR/hp_ctrl2.bin" \
    > "$RESULTS_DIR/hp_disp.log" 2>&1

# stdout'ta High-priority: YES (src1 için 2x); NO mesajı yok, sadece YES olanlar log'lanıyor
YES_COUNT=$(grep -c "High-priority: YES" "$RESULTS_DIR/hp_disp.log" || true)

# 2 src1 entries → 2 YES; src2'ler için ayrı log yok ama özet satırında "Priority=2" olmalı
assert_eq "dispatcher: YES count = 2 (src1 entries)" "2" "$YES_COUNT"

# Dispatcher exit özetinde "Routed=4" ve "Priority=2" görmeli
assert_grep "dispatcher summary: 'Routed=4'" "Routed=4" "$RESULTS_DIR/hp_disp.log"
assert_grep "dispatcher summary: 'Priority=2'" "Priority=2" "$RESULTS_DIR/hp_disp.log"

print_summary
