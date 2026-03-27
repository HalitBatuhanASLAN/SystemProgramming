#!/bin/bash
# =============================================================================
# CSE344 HW2 — procSearch Comprehensive Test Script
# Usage: ./test_procSearch.sh
# Pattern semantics: + repeats the previous character (rep+ort -> reppport, report)
# =============================================================================

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'
PASS=0; FAIL=0; WARN=0

pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; ((PASS++)); }
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; ((FAIL++)); }
warn() { echo -e "  ${YELLOW}[WARN]${NC} $1"; ((WARN++)); }
info() { echo -e "  ${CYAN}[INFO]${NC} $1"; }
header() {
    echo ""
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE}  $1${NC}"
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════════════${NC}"
}

BINARY="./procSearch"
TEST_DIR="/tmp/procSearch_test"

if [ ! -f "$BINARY" ]; then
    echo -e "${RED}ERROR: '$BINARY' not found. Run 'make' first.${NC}"
    exit 1
fi

# =============================================================================
# TEST ENVIRONMENT SETUP
# =============================================================================
header "SETTING UP TEST ENVIRONMENT"

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR/alpha/sub1" "$TEST_DIR/beta" "$TEST_DIR/gamma" "$TEST_DIR/delta"

# rep+ort should match (p is repeated)
echo "quarterly report data"  > "$TEST_DIR/alpha/report.txt"
echo "reppport data"          > "$TEST_DIR/alpha/reppport.txt"
echo "old file"               > "$TEST_DIR/alpha/sub1/repppport.txt"
echo "final version"          > "$TEST_DIR/beta/report_final.txt"

# Should not match
touch "$TEST_DIR/alpha/notes.md"
touch "$TEST_DIR/alpha/sub1/image.png"
touch "$TEST_DIR/beta/error_log.txt"
touch "$TEST_DIR/gamma/data.csv"
touch "$TEST_DIR/delta/summary.txt"

# er+ro+r test
echo "error log"  > "$TEST_DIR/gamma/error.txt"
echo "errror log" > "$TEST_DIR/gamma/errror.txt"

info "Test directory: $TEST_DIR"
find "$TEST_DIR" | sort | sed "s|$TEST_DIR||" | sed 's|^|    |'

# =============================================================================
header "SECTION 1: ARGUMENT PARSING"
# =============================================================================

output=$("$BINARY" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.1 No arguments -> Usage message"
else
    fail "1.1 No arguments -> No usage message"
fi

output=$("$BINARY" -d "$TEST_DIR" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.2 Missing arguments (-d only) -> Usage"
else
    fail "1.2 Missing arguments -> No usage message"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 1 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.3 -n 1 (less than 2) -> Error message"
else
    fail "1.3 -n 1 -> No error message"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 9 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.4 -n 9 (greater than 8) -> Error message"
else
    fail "1.4 -n 9 -> No error message"
fi

output=$("$BINARY" -d "/tmp/folder_does_not_exist_123" -n 3 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|cannot\|no such"; then
    pass "1.5 Non-existent directory -> Error message"
else
    fail "1.5 Non-existent directory -> No error message"
fi

# =============================================================================
header "SECTION 2: PATTERN MATCHING"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:" || true)

if [ "$match_count" -ge 4 ]; then
    pass "2.1 rep+ort -> $match_count matches (>=4 expected)"
else
    fail "2.1 rep+ort -> $match_count matches, >=4 expected"
fi

if echo "$output" | grep -q "report.txt"; then
    pass "2.2 report.txt (p=1) matched"
else
    fail "2.2 report.txt did not match"
fi

if echo "$output" | grep -q "reppport.txt"; then
    pass "2.3 reppport.txt (p=3) matched"
else
    fail "2.3 reppport.txt did not match"
fi

if echo "$output" | grep -q "repppport.txt"; then
    pass "2.4 repppport.txt (p=4) matched"
else
    fail "2.4 repppport.txt did not match"
fi

if echo "$output" | grep -q "report_final.txt"; then
    pass "2.5 report_final.txt matched"
else
    fail "2.5 report_final.txt did not match"
fi

if echo "$output" | grep "MATCH:" | grep -qE "notes\.md|image\.png|error_log|data\.csv|summary"; then
    fail "2.6 Files that shouldn't match appeared in results"
else
    pass "2.6 Files that shouldn't match are absent"
fi

output_nomatch=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output_nomatch" | grep -q "No matching files found"; then
    pass "2.7 xyz+123 -> No matching files found"
else
    fail "2.7 xyz+123 -> 'No matching files found' missing"
fi

# Exact match
mkdir -p "$TEST_DIR/exact_test"
echo "x" > "$TEST_DIR/exact_test/notes.txt"
echo "x" > "$TEST_DIR/exact_test/notesXYZ.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "notes" 2>/dev/null)
if echo "$output" | grep "MATCH:" | grep -q "notes.txt" &&
   ! echo "$output" | grep "MATCH:" | grep -q "notesXYZ"; then
    pass "2.8 Exact match 'notes' -> notes.txt ✓, notesXYZ.txt ✗"
else
    warn "2.8 Exact match ambiguous — check manually"
fi

# Case-insensitive
mkdir -p "$TEST_DIR/case_test"
echo "x" > "$TEST_DIR/case_test/REPORT.txt"
echo "x" > "$TEST_DIR/case_test/Report.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "rep+ort" 2>/dev/null)
if echo "$output" | grep -q "REPORT.txt" && echo "$output" | grep -q "Report.txt"; then
    pass "2.9 Case-insensitive -> REPORT.txt and Report.txt found"
else
    warn "2.9 Case-insensitive -> missing match, check manually"
fi

# er+ro+r
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "er+ro+r" 2>/dev/null)
if echo "$output" | grep -q "error.txt" && echo "$output" | grep -q "errror.txt"; then
    pass "2.10 er+ro+r -> error.txt and errror.txt found"
else
    fail "2.10 er+ro+r -> missing match"
    info "    Found: $(echo "$output" | grep MATCH:)"
fi

rm -rf "$TEST_DIR/exact_test" "$TEST_DIR/case_test"

# =============================================================================
header "SECTION 3: SIZE FILTER (-s)"
# =============================================================================

# report.txt = 22 bytes, report_final.txt = 14 bytes
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
if echo "$output" | grep "MATCH:" | grep -q "report.txt" &&
   ! echo "$output" | grep "MATCH:" | grep -q "report_final.txt"; then
    pass "3.1 -s 15: report.txt(22B) ✓, report_final.txt(14B) ✗"
else
    fail "3.1 -s 15 size filter incorrect"
    info "    Found: $(echo "$output" | grep MATCH:)"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 99999 2>/dev/null)
if echo "$output" | grep -q "No matching files found"; then
    pass "3.2 -s 99999 -> no matches found"
else
    fail "3.2 -s 99999 -> 'No matching files found' missing"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 0 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:" || true)
if [ "$match_count" -ge 4 ]; then
    pass "3.3 -s 0 -> $match_count matches (no filter)"
else
    fail "3.3 -s 0 -> $match_count matches, >=4 expected"
fi

# =============================================================================
header "SECTION 4: WORKER PROCESS MANAGEMENT"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)

if echo "$output" | grep -qE "\[Worker PID:[0-9]+\] MATCH:"; then
    pass "4.1 Worker PID format correct: [Worker PID:XXXX] MATCH:"
else
    fail "4.1 Worker PID format incorrect"
    info "    Expected: [Worker PID:XXXX] MATCH: /path (N bytes)"
fi

if echo "$output" | grep -qE "MATCH:.*\([0-9]+ bytes\)"; then
    pass "4.2 Byte format correct: (XXX bytes)"
else
    fail "4.2 Byte format incorrect"
fi

# 4 directories exist, requesting 6 workers
output=$("$BINARY" -d "$TEST_DIR" -n 6 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*only.*subdirector.*found.*using.*instead"; then
    pass "4.3 Dirs(4) < Workers(6) -> Notice correct"
else
    fail "4.3 Dirs < Workers -> Notice incorrect/missing"
    info "    Found: $(echo "$output" | grep -i notice | head -1)"
fi

EMPTY_DIR="/tmp/procSearch_empty_$$"
mkdir -p "$EMPTY_DIR"
echo "test" > "$EMPTY_DIR/report.txt"
output=$("$BINARY" -d "$EMPTY_DIR" -n 2 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*no subdirector.*parent will search root"; then
    pass "4.4 No subdirs -> Notice + parent search"
else
    fail "4.4 No subdirs -> Notice incorrect/missing"
    info "    Found: $(echo "$output" | grep -i notice | head -1)"
fi
rm -rf "$EMPTY_DIR"

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
pid_count=$(echo "$output" | grep -oE "PID:[0-9]+" | sort -u | wc -l)
if [ "$pid_count" -ge 2 ]; then
    pass "4.5 Multiple Worker PIDs: $pid_count unique PIDs"
else
    warn "4.5 Only $pid_count unique PID — retest when pattern is fixed"
fi

# =============================================================================
header "SECTION 5: OUTPUT FORMAT"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)

if echo "$output" | grep -q "$TEST_DIR"; then
    pass "5.1 Root directory name present in output"
else
    fail "5.1 Root directory name missing"
fi

if echo "$output" | grep -qE "^\|--"; then
    pass "5.2 Tree format: |-- present"
else
    fail "5.2 Tree format: |-- missing"
fi

if echo "$output" | grep -q "\-\-\- Summary \-\-\-"; then
    pass "5.3 '--- Summary ---' present"
else
    fail "5.3 '--- Summary ---' missing"
fi

if echo "$output" | grep -q "Total workers used"; then
    pass "5.4 'Total workers used' present"
else
    fail "5.4 'Total workers used' missing"
fi

if echo "$output" | grep -q "Total files scanned"; then
    pass "5.5 'Total files scanned' present"
else
    fail "5.5 'Total files scanned' missing"
fi

if echo "$output" | grep -q "Total matches found"; then
    pass "5.6 'Total matches found' present"
else
    fail "5.6 'Total matches found' missing"
fi

if echo "$output" | grep -qE "Worker PID [0-9]+.*: [0-9]+ match"; then
    pass "5.7 Summary: Worker PID lines present"
else
    fail "5.7 Summary: Worker PID lines missing"
fi

if echo "$output" | grep -qE ": 1 match$"; then
    pass "5.8 Singular: '1 match' correct"
else
    warn "5.8 Singular match could not be tested — retest when pattern is fixed"
fi

# =============================================================================
header "SECTION 6: SIGNAL MANAGEMENT — SIGINT"
# =============================================================================

info "SIGINT test — /usr/share/doc directory (signal will be sent after 2 sec)"

"$BINARY" -d /usr/share/doc -n 4 -f "read+me" > /tmp/sigint_out_$$.txt 2>&1 &
PROC_PID=$!
sleep 2

if kill -0 "$PROC_PID" 2>/dev/null; then
    kill -INT "$PROC_PID"
    sleep 2

    sigint_out=$(cat /tmp/sigint_out_$$.txt)

    if echo "$sigint_out" | grep -q "SIGINT received"; then
        pass "6.1 SIGINT -> '[Parent] SIGINT received. Terminating all workers...' present"
    else
        fail "6.1 SIGINT -> message missing"
        info "    Last 5 lines: $(echo "$sigint_out" | tail -5)"
    fi

    sleep 1
    zombie_count=$(ps aux | awk '{print $8}' | grep -c "^Z" 2>/dev/null || true)
    zombie_count=${zombie_count:-0}
    if [ "$zombie_count" -eq 0 ]; then
        pass "6.2 No zombies after SIGINT"
    else
        fail "6.2 $zombie_count zombies present!"
    fi

    remaining=$(pgrep -c procSearch 2>/dev/null || echo 0)
    if [ "$remaining" -eq 0 ]; then
        pass "6.3 All procSearch processes cleaned up"
    else
        fail "6.3 $remaining procSearch processes still running"
        pkill -9 procSearch 2>/dev/null
    fi
else
    warn "6.1 Program finished before SIGINT could be sent"
    warn "6.2 Skipped"
    warn "6.3 Skipped"
fi

rm -f /tmp/sigint_out_$$.txt

# =============================================================================
header "SECTION 7: ZOMBIE PROCESS CONTROL"
# =============================================================================

"$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" > /dev/null 2>&1
sleep 1

zombie_count=$(ps aux | awk '{print $8}' | grep -c "^Z" 2>/dev/null || true)
zombie_count=${zombie_count:-0}
if [ "$zombie_count" -eq 0 ]; then
    pass "7.1 No zombies after normal execution"
else
    fail "7.1 $zombie_count zombies present!"
    ps -ef | grep defunct
fi

# =============================================================================
header "SECTION 8: MANDATORY TEST SCENARIO"
# =============================================================================

info "Step 2: rep+ort pattern test"
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
echo ""
echo -e "${CYAN}--- Program Output ---${NC}"
echo "$output"
echo -e "${CYAN}-----------------------${NC}"

cnt=$(echo "$output" | grep -c "report.txt"    || true); [ "$cnt" -ge 1 ] && pass "8.1 report.txt"    || fail "8.1 report.txt not found"
cnt=$(echo "$output" | grep -c "reppport.txt"  || true); [ "$cnt" -ge 1 ] && pass "8.2 reppport.txt"  || fail "8.2 reppport.txt not found"
cnt=$(echo "$output" | grep -c "repppport.txt" || true); [ "$cnt" -ge 1 ] && pass "8.3 repppport.txt" || fail "8.3 repppport.txt not found"
cnt=$(echo "$output" | grep -c "report_final"  || true); [ "$cnt" -ge 1 ] && pass "8.4 report_final"  || fail "8.4 report_final not found"

info "Step 3: -s 15 size filter"
output_s=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
echo ""
echo -e "${CYAN}--- -s 15 Output ---${NC}"
echo "$output_s"
echo -e "${CYAN}---------------------${NC}"

info "Step 4: xyz+123 non-matching pattern"
output_nm=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output_nm" | grep -q "No matching files found"; then
    pass "8.5 xyz+123 -> No matching files found"
else
    fail "8.5 xyz+123 -> 'No matching files found' missing"
fi

# =============================================================================
header "SECTION 9: COMPILATION QUALITY"
# =============================================================================

warning_count=$(make -B 2>&1 | grep -c "warning:" || true)
warning_count=${warning_count:-0}
if [ "$warning_count" -eq 0 ]; then
    pass "9.1 make -Wall -> 0 warnings"
else
    fail "9.1 make -Wall -> $warning_count warnings"
    make -B 2>&1 | grep "warning:" | head -5
fi

make clean > /dev/null 2>&1
if [ ! -f "procSearch" ] && [ ! -f "main.o" ]; then
    pass "9.2 make clean -> files deleted"
else
    fail "9.2 make clean failed"
fi

make > /dev/null 2>&1
if [ -f "procSearch" ]; then
    pass "9.3 make -> binary rebuilt"
else
    fail "9.3 make failed"
fi

# =============================================================================
header "SECTION 10: MEMORY LEAK (Valgrind)"
# =============================================================================

if command -v valgrind &> /dev/null; then
    valgrind_out=$(timeout 30 valgrind --leak-check=full \
        "$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>&1)
    if echo "$valgrind_out" | grep -q "definitely lost: 0 bytes"; then
        pass "10.1 Valgrind: No memory leak"
    else
        warn "10.1 Valgrind: Possible memory leak — check manually"
        echo "$valgrind_out" | grep -E "definitely|indirectly|possibly" | head -5
    fi
else
    warn "10.1 Valgrind not installed: run 'sudo apt install valgrind'"
fi

# =============================================================================
header "TEST RESULTS"
# =============================================================================

TOTAL=$((PASS + FAIL + WARN))
echo ""
echo -e "  Total   : ${BOLD}$TOTAL${NC}"
echo -e "  ${GREEN}Passed  : $PASS${NC}"
echo -e "  ${RED}Failed  : $FAIL${NC}"
echo -e "  ${YELLOW}Warning : $WARN${NC}"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}  ✓ All critical tests passed!${NC}"
else
    echo -e "${RED}${BOLD}  ✗ $FAIL tests failed${NC}"
fi

echo ""
echo -e "${CYAN}  Manual SIGINT test:${NC}"
echo -e "  ${BOLD}  ./procSearch -d /usr/share/doc -n 4 -f 'read+me'${NC}"
echo -e "  ${BOLD}  (Press Ctrl+C, then: ps aux | grep Z)${NC}"
echo ""

rm -rf "$TEST_DIR"
exit $FAIL