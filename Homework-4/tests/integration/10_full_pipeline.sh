#!/usr/bin/env bash
# =============================================================================
# 10_full_pipeline.sh - End-to-end pipeline çalışma testi
# =============================================================================
# Bu test PDF'in örnek senaryosunu komple çalıştırır ve tüm bileşenlerin
# (Reader → Dispatcher → Analyzer × 4 → Aggregator) birlikte doğru
# çalıştığını doğrular.
# =============================================================================

source "$(dirname "${BASH_SOURCE[0]}")/../helpers/common.sh"

print_test_header "Integration Test 10: Full Pipeline"
ensure_environment

cd "$FIXTURES_DIR"

# ── 10.1 Baseline: PDF örneğine yakın senaryo ────────────────────────────────
log_subsection "10.1 Baseline run with realistic params"

run_with_timeout 30 "$RESULTS_DIR/full.out" "$RESULTS_DIR/full.err" \
    timeout 20 "$ANALYZER_BIN" -c simple.conf -f simple_priority.txt -k "error,fail,timeout" \
    -t 2 -w 2 -a 16 -b 16 -d 8 -T 5 \
    -o "$RESULTS_DIR/full.txt" -O "$RESULTS_DIR/full.bin"
EXIT=$?

assert_exit_code "baseline: exit 0"               "0" "$EXIT"
assert_file_exists "baseline: .txt output"       "$RESULTS_DIR/full.txt"
assert_file_exists "baseline: .bin output"       "$RESULTS_DIR/full.bin"
assert_file_nonempty "baseline: .txt non-empty"  "$RESULTS_DIR/full.txt"

# ── 10.2 stdout PID-tagged log mesajları ──────────────────────────────────
log_subsection "10.2 stdout: PID-tagged log messages"

assert_grep "stdout: 'Parent started'"   "Parent started"   "$RESULTS_DIR/full.out"
assert_grep "stdout: 'Forking Reader'"   "Forking Reader"   "$RESULTS_DIR/full.out"
assert_grep "stdout: 'Forking Dispatcher'" "Forking Dispatcher" "$RESULTS_DIR/full.out"
assert_grep "stdout: 'Forking Analyzer'" "Forking Analyzer" "$RESULTS_DIR/full.out"
assert_grep "stdout: 'Forking Aggregator'" "Forking Aggregator" "$RESULTS_DIR/full.out"
assert_grep "stdout: '[PID:'" "\[PID:" "$RESULTS_DIR/full.out"

# ── 10.3 4 Analyzer'ın hepsi başlamış ───────────────────────────────────────
log_subsection "10.3 All 4 Analyzers started"

ANALYZER_COUNT=$(grep -c "Analyzer .* started" "$RESULTS_DIR/full.out" || echo 0)
assert_eq "4 Analyzer process started" "4" "$ANALYZER_COUNT"

# ── 10.4 Reader'lar parser thread'i bitirmiş ────────────────────────────────
log_subsection "10.4 All Readers dispatched their parser thread"

# 'Parser thread: dispatched' satırı her reader için 1 kez olmalı (2 reader)
DISPATCH_COUNT=$(grep -c "Parser thread: dispatched" "$RESULTS_DIR/full.out" || echo 0)
assert_eq "2 reader parser threads" "2" "$DISPATCH_COUNT"

# ── 10.5 Dispatcher EOF marker'ları forward etti ────────────────────────────
log_subsection "10.5 Dispatcher EOF forwarding"

assert_grep "EOF markers forwarded to Region B" \
    "All EOF markers forwarded" "$RESULTS_DIR/full.out"

# ── 10.6 Aggregator tüm 4 LEVEL sonucunu aldı ──────────────────────────────
log_subsection "10.6 Aggregator received all 4 results"

for lvl in ERROR WARN INFO DEBUG; do
    assert_grep "Aggregator: $lvl result received" \
        "$lvl result received" "$RESULTS_DIR/full.out"
done

# ── 10.7 Çıktı dosyaları yazılmış ───────────────────────────────────────────
log_subsection "10.7 Aggregator wrote output files"

assert_grep "Aggregator: 'Output files written'" \
    "Output files written" "$RESULTS_DIR/full.out"

# ── 10.8 Process'ler temiz çıktı (no zombies) ──────────────────────────────
log_subsection "10.8 No zombie processes after exit"

# 'Parent started.*successfully' veya 'Program terminated successfully'
assert_grep "stdout: 'Program terminated successfully'" \
    "Program terminated successfully" "$RESULTS_DIR/full.out"

# Process tablosunda kalıntı olmamalı
sleep 0.3
LEFTOVERS=$(pgrep -af '/analyzer ' 2>/dev/null | grep -v grep | wc -l)
assert_eq "no leftover analyzer processes" "0" "$LEFTOVERS"

# ── 10.9 Reporting thread mesajları ─────────────────────────────────────────
log_subsection "10.9 'Reporting thread' messages (lowest TID per analyzer)"

# Her analyzer'da 1 reporting thread olmalı → 4 toplam
RT_COUNT=$(grep -c "Reporting thread (lowest TID)" "$RESULTS_DIR/full.out" || echo 0)
assert_eq "4 reporting threads (1 per analyzer)" "4" "$RT_COUNT"

# Her LEVEL için bir reporting thread mesajı olmalı
for lvl in ERROR WARN INFO DEBUG; do
    assert_grep "$lvl reporting thread" \
        "Reporting thread.*Level: $lvl" "$RESULTS_DIR/full.out"
done

# ── 10.10 Worker thread mesajları ───────────────────────────────────────────
log_subsection "10.10 Worker thread messages"

# 4 Analyzer × 2 Worker = 8 'Worker N started' mesajı
WORKER_START=$(grep -c "Worker .* started" "$RESULTS_DIR/full.out" || echo 0)
assert_eq "8 'Worker N started' messages (4×2)" "8" "$WORKER_START"

# Aynı sayıda 'Worker N done' mesajı olmalı
WORKER_DONE=$(grep -c "Worker .* done" "$RESULTS_DIR/full.out" || echo 0)
assert_eq "8 'Worker N done' messages" "8" "$WORKER_DONE"

# ── 10.11 SYSTEM_SUMMARY block ──────────────────────────────────────────────
log_subsection "10.11 Final SYSTEM_SUMMARY block"

assert_grep "SYSTEM SUMMARY header" "SYSTEM SUMMARY" "$RESULTS_DIR/full.out"
assert_grep "summary: 'Keywords'"    "Keywords"      "$RESULTS_DIR/full.out"
assert_grep "summary: 'Log files'"   "Log files"     "$RESULTS_DIR/full.out"
assert_grep "summary: 'Total entries'" "Total entries" "$RESULTS_DIR/full.out"

# ── 10.12 Internal consistency: total_entries matches sum ──────────────────
log_subsection "10.12 SUMMARY total = sum of LEVEL entries"

# .txt çıktısından LEVEL bazlı entry sayılarını topla
ERROR_E=$(extract_level_col "$RESULTS_DIR/full.txt" "ERROR" 2)
WARN_E=$(extract_level_col "$RESULTS_DIR/full.txt" "WARN" 2)
INFO_E=$(extract_level_col "$RESULTS_DIR/full.txt" "INFO" 2)
DEBUG_E=$(extract_level_col "$RESULTS_DIR/full.txt" "DEBUG" 2)
SUM_E=$((ERROR_E + WARN_E + INFO_E + DEBUG_E))

# stdout'taki "Total entries : N" satırını çek
TOTAL_E=$(grep "Total entries" "$RESULTS_DIR/full.out" | awk -F': ' '{print $2}' | tail -1)

assert_eq "summary entries == sum(level entries)" "$SUM_E" "$TOTAL_E"

print_summary
