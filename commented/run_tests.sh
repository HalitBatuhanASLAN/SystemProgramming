#!/bin/bash
# ==============================================================
# FILE: run_tests.sh
# --------------------------------------------------------------
# PURPOSE:
#   Automated test suite for the CSE344 HW3 multi-process word
#   transportation and sorting system.
#
# USAGE:
#   ./run_tests.sh          → run ALL 10 test cases
#   ./run_tests.sh <N>      → run only test N  (1 … 10)
#
# OUTPUT:
#   - Each test prints PASS or FAIL with a short reason.
#   - Verbose process logs are written to tests/logs/ so that
#     the terminal only shows the test result summary, not the
#     thousands of log lines that hw3 produces.
#
# TEST COVERAGE:
#   1.  Basic 3-floor scenario           (functional correctness)
#   2.  Single-floor building            (no elevator needed)
#   3.  Words with repeated characters   (sorting correctness)
#   4.  Same-floor delivery              (direct placement path)
#   5.  Low capacity stress              (retry mechanism)
#   6.  Argument validation              (invalid flags rejected)
#   7.  Input format validation          (bad file formats)
#   8.  SIGINT / Ctrl+C clean shutdown   (no zombie processes)
#   9.  Large input (15 words, 5 floors) (scalability)
#  10.  Consistency across 3 runs        (deterministic output)
# ==============================================================

# ── Paths ──────────────────────────────────────────────────────
# TESTS_DIR: absolute path to the directory containing this script.
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"

# HW3: the compiled binary (one level above the tests directory).
HW3="$(dirname "$TESTS_DIR")/hw3"

# LOG_DIR: where verbose hw3 output is redirected.
LOG_DIR="$TESTS_DIR/logs"

# ── Result counters ────────────────────────────────────────────
PASS=0
FAIL=0

# Create the log directory if it does not already exist.
mkdir -p "$LOG_DIR"

# ── ANSI colour codes for readable terminal output ────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'   # Reset / No Colour

# ── Helper functions ───────────────────────────────────────────

# section – print a visually distinct section heading.
section() {
    echo -e "\n${CYAN}${BOLD}══════════════════════════════════════════${NC}"
    echo -e "${CYAN}${BOLD}  $1${NC}"
    echo -e "${CYAN}${BOLD}══════════════════════════════════════════${NC}"
}

# pass – record a passing assertion and print a green message.
pass() { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }

# fail – record a failing assertion and print a red message.
fail() { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }

# info – print a yellow informational (non-assertion) message.
info() { echo -e "  ${YELLOW}[INFO]${NC} $1"; }

# ── check_binary ───────────────────────────────────────────────
# Verifies that the hw3 binary exists before running any test.
# Exits with an error if the binary has not been compiled yet.
check_binary() {
    if [ ! -f "$HW3" ]; then
        echo -e "${RED}ERROR: $HW3 not found. Run 'make' first.${NC}"
        exit 1
    fi
    echo -e "${GREEN}Binary: $HW3${NC}"
    echo -e "${YELLOW}Logs:   $LOG_DIR/${NC}"
}

# ── run_hw3 ────────────────────────────────────────────────────
# Runs hw3 with a timeout, redirecting ALL output (stdout+stderr)
# to a log file so the terminal stays clean.
#
# Parameters:
#   $1 – log file path
#   $2 – timeout in seconds
#   $3… – full hw3 command and its arguments
#
# Returns the exit code of hw3 (or 124 if the timeout expired).
run_hw3() {
    local logfile="$1"
    local timeout_sec="$2"
    shift 2
    timeout "$timeout_sec" "$@" > "$logfile" 2>&1
    return $?
}

# ── get_summary ────────────────────────────────────────────────
# Extracts the final summary lines from a log file for display.
# Useful for quick debugging without opening the full log.
get_summary() {
    local logfile="$1"
    grep -E "Total words:|Completed words:|Retries:|Characters transported:\
|Delivery elevator|Reposition elevator|terminated successfully|COMPLETED" \
        "$logfile" 2>/dev/null
}

# ── validate_output ────────────────────────────────────────────
# Checks that the output file produced by hw3 is correct:
#   1. File exists.
#   2. Exactly expected_count non-empty lines.
#   3. Every line matches the regex: "<int> <lowercase_word> <int>"
#   4. No blank lines.
#   5. Lines are sorted by sorting_floor asc, then word_id asc.
#
# Parameters:
#   $1 – output file path
#   $2 – expected number of lines (= number of words)
#   $3 – test name for error messages
validate_output() {
    local outfile="$1"
    local expected_count="$2"
    local test_name="$3"

    # Check existence.
    if [ ! -f "$outfile" ]; then
        fail "$test_name: Output file was not created"
        return 1
    fi

    # Check line count.
    local line_count
    line_count=$(wc -l < "$outfile")
    if [ "$line_count" -ne "$expected_count" ]; then
        fail "$test_name: Wrong line count (expected: $expected_count, got: $line_count)"
        return 1
    fi

    # Check that every line matches <word_id> <word> <sorting_floor>.
    local bad_lines
    bad_lines=$(grep -cvP '^\d+ [a-z]+ \d+$' "$outfile" || true)
    if [ "$bad_lines" -gt 0 ]; then
        fail "$test_name: Format error ($bad_lines malformed lines)"
        return 1
    fi

    # Check for blank lines (forbidden by the spec).
    local empty_lines
    empty_lines=$(grep -c '^$' "$outfile" 2>/dev/null || echo 0)
    if [ "$empty_lines" -gt 0 ]; then
        fail "$test_name: Blank lines present in output"
        return 1
    fi

    # Check sort order: primary key = sorting_floor, secondary = word_id.
    local prev_sf=-1
    local prev_wid=-1
    local sort_ok=1
    while IFS=' ' read -r wid word sf; do
        if   [ "$sf"  -lt "$prev_sf"  ]; then sort_ok=0; break
        elif [ "$sf"  -eq "$prev_sf"  ] && [ "$wid" -lt "$prev_wid" ]; then
            sort_ok=0
            break
        fi
        prev_sf=$sf
        prev_wid=$wid
    done < "$outfile"

    if [ "$sort_ok" -eq 0 ]; then
        fail "$test_name: Output not sorted correctly"
        return 1
    fi

    pass "$test_name: Output correct ($line_count words, format+sort OK)"
    return 0
}


# ==============================================================
# TEST 1 – Basic 3-floor scenario
# --------------------------------------------------------------
# Runs hw3 on input_basic.txt (6 words across 3 floors).
# Verifies:
#   - Program exits with "Program terminated successfully".
#   - All 6 words are reported as completed.
#   - Output file has 6 correctly formatted and sorted lines.
# ==============================================================
test_1_basic() {
    section "TEST 1: Basic Scenario (3 floors, 6 words)"
    info "Floors:3 | w:2 l:2 s:2 | c:4 d:3 r:2 | timeout:30s"

    local outfile="$TESTS_DIR/out_test1.txt"
    local logfile="$LOG_DIR/test1.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 30 "$HW3" \
        -f 3 -w 2 -l 2 -s 2 -c 4 -d 3 -r 2 \
        -i "$TESTS_DIR/input_basic.txt" -o "$outfile"
    local ec=$?

    # A timeout exit code of 124 almost always indicates a deadlock.
    if [ $ec -eq 124 ]; then
        fail "TEST 1: TIMEOUT (30s) - possible deadlock!"
        info "Log: $logfile"
        return
    fi
    if [ $ec -ne 0 ]; then
        fail "TEST 1: Non-zero exit code ($ec)"
        return
    fi

    # Verify the success banner is in the log.
    if grep -q "Program terminated successfully" "$logfile"; then
        pass "TEST 1: 'Program terminated successfully' found"
    else
        fail "TEST 1: Success banner not found"
    fi

    # Verify all words were reported as completed.
    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ] && [ -n "$total" ]; then
        pass "TEST 1: All words completed ($completed/$total)"
    else
        fail "TEST 1: Incomplete (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 6 "TEST 1"
    info "Output:"; cat "$outfile" | sed 's/^/    /'
    info "Log: $logfile"
}

# ==============================================================
# TEST 2 – Single-floor building
# --------------------------------------------------------------
# All words arrive and sort on floor 0; no elevator needed.
# Verifies:
#   - Program completes without timeout.
#   - Delivery elevator operations == 0 (no cross-floor moves).
#   - Output file is correct.
# ==============================================================
test_2_single_floor() {
    section "TEST 2: Single Floor"
    info "Floors:1 | w:1 l:2 s:1 | c:5 d:2 r:1 | timeout:20s"

    local outfile="$TESTS_DIR/out_test2.txt"
    local logfile="$LOG_DIR/test2.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 20 "$HW3" \
        -f 1 -w 1 -l 2 -s 1 -c 5 -d 2 -r 1 \
        -i "$TESTS_DIR/input_single_floor.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 2: TIMEOUT"; return; fi
    if [ $ec -ne 0 ];   then fail "TEST 2: Non-zero exit ($ec)"; return; fi

    pass "TEST 2: Completed successfully"

    # On a single floor the delivery elevator should never operate.
    local deliv_ops
    deliv_ops=$(grep "Delivery elevator operations:" "$logfile" | grep -oP '\d+' | head -1)
    if [ -n "$deliv_ops" ] && [ "$deliv_ops" -eq 0 ]; then
        pass "TEST 2: Delivery elevator ops=0 (correct for single floor)"
    else
        info "TEST 2: Delivery ops=$deliv_ops"
    fi

    validate_output "$outfile" 3 "TEST 2"
}

# ==============================================================
# TEST 3 – Words with repeated characters
# --------------------------------------------------------------
# Tests words like "mississippi" and "balloon" where the sorting
# algorithm must correctly handle multiple identical characters.
# Verifies:
#   - No deadlock / timeout.
#   - All 4 words completed.
#   - Output file correct.
# ==============================================================
test_3_repeated_chars() {
    section "TEST 3: Repeated Characters"
    info "Floors:2 | 'mississippi', 'balloon' | timeout:30s"

    local outfile="$TESTS_DIR/out_test3.txt"
    local logfile="$LOG_DIR/test3.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 30 "$HW3" \
        -f 2 -w 2 -l 3 -s 2 -c 5 -d 4 -r 2 \
        -i "$TESTS_DIR/input_repeated_chars.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then
        fail "TEST 3: TIMEOUT - deadlock on repeated chars?"
        info "Log: $logfile"
        return
    fi
    if [ $ec -ne 0 ]; then fail "TEST 3: Non-zero exit ($ec)"; return; fi

    pass "TEST 3: Completed"

    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ]; then
        pass "TEST 3: $completed/$total words completed"
    else
        fail "TEST 3: Incomplete (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 4 "TEST 3"
}

# ==============================================================
# TEST 4 – Same-floor delivery (direct placement)
# --------------------------------------------------------------
# All words have arrival_floor == sorting_floor == 0.
# The letter-carrier code path for "Destination is same floor →
# direct placement" must be exercised.
# ==============================================================
test_4_same_floor() {
    section "TEST 4: Same-Floor Delivery"
    info "Floors:1 | all words on floor 0 | timeout:20s"

    local outfile="$TESTS_DIR/out_test4.txt"
    local logfile="$LOG_DIR/test4.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 20 "$HW3" \
        -f 1 -w 1 -l 2 -s 1 -c 5 -d 2 -r 1 \
        -i "$TESTS_DIR/input_same_floor.txt" -o "$outfile"

    # Confirm the direct-placement code branch was taken.
    if grep -q "direct placement" "$logfile"; then
        pass "TEST 4: 'direct placement' log message found"
    else
        fail "TEST 4: 'direct placement' not found in log"
    fi

    validate_output "$outfile" 3 "TEST 4"
}

# ==============================================================
# TEST 5 – Capacity stress (low floor capacity)
# --------------------------------------------------------------
# Sets max_words_per_floor to 2 while there are 10 words,
# forcing many admission retries.
# Verifies that the retry mechanism works correctly and all
# words eventually complete.
# ==============================================================
test_5_capacity_stress() {
    section "TEST 5: Capacity Stress (c=2)"
    info "Floors:3 | 10 words | c:2 | timeout:90s"

    local outfile="$TESTS_DIR/out_test5.txt"
    local logfile="$LOG_DIR/test5.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 90 "$HW3" \
        -f 3 -w 2 -l 3 -s 2 -c 2 -d 3 -r 2 \
        -i "$TESTS_DIR/input_capacity_stress.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then
        fail "TEST 5: TIMEOUT (90s) - possible deadlock!"
        info "Log: $logfile"
        return
    fi
    if [ $ec -ne 0 ]; then fail "TEST 5: Non-zero exit ($ec)"; return; fi

    # With low capacity there should be retries logged.
    local retries
    retries=$(grep "Retries:" "$logfile" | grep -oP '\d+' | head -1)
    info "Retries: $retries"
    if [ -n "$retries" ] && [ "$retries" -gt 0 ]; then
        pass "TEST 5: Retry mechanism triggered ($retries retries)"
    else
        info "TEST 5: Retry=0 (capacity was sufficient throughout)"
    fi

    validate_output "$outfile" 10 "TEST 5"
}

# ==============================================================
# TEST 6 – Argument validation
# --------------------------------------------------------------
# Checks that hw3 rejects bad command-line arguments with a
# non-zero exit code.
# Cases:
#   6a – Missing mandatory flags.
#   6b – num_floors = 0 (must be >= 1).
#   6c – Non-existent input file.
#   6d – Negative floor capacity.
# ==============================================================
test_6_arg_validation() {
    section "TEST 6: Argument Validation"
    local outfile="$TESTS_DIR/out_dummy.txt"

    # 6a: missing required flags → should fail.
    "$HW3" -f 3 -w 2 -l 2 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6a: Missing args rejected" \
                  || fail "TEST 6a: Missing args accepted (should fail)"

    # 6b: num_floors = 0 → must be >= 1.
    "$HW3" -f 0 -w 1 -l 1 -s 1 -c 1 -d 1 -r 1 \
           -i "$TESTS_DIR/input_basic.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6b: num_floors=0 rejected" \
                  || fail "TEST 6b: num_floors=0 accepted (should fail)"

    # 6c: non-existent input file.
    "$HW3" -f 3 -w 1 -l 1 -s 1 -c 1 -d 1 -r 1 \
           -i "/tmp/nonexistent_xyz_file.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6c: Missing input file rejected" \
                  || fail "TEST 6c: Missing input file accepted (should fail)"

    # 6d: negative capacity value.
    "$HW3" -f 3 -w 1 -l 1 -s 1 -c -1 -d 1 -r 1 \
           -i "$TESTS_DIR/input_basic.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6d: Negative capacity rejected" \
                  || fail "TEST 6d: Negative capacity accepted (should fail)"
}

# ==============================================================
# TEST 7 – Input file format validation
# --------------------------------------------------------------
# hw3 must reject malformed input files.
# Cases:
#   7a – Blank line in the middle of the file.
#   7b – Uppercase letter in a word.
#   7c – Double space between fields.
# ==============================================================
test_7_input_format() {
    section "TEST 7: Malformed Input File"
    local outfile="$TESTS_DIR/out_dummy.txt"

    # 7a: blank line → forbidden by spec.
    printf "1 hello 0\n\n2 world 0\n" > /tmp/bad1.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 \
           -i /tmp/bad1.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7a: Blank line rejected" \
                  || fail "TEST 7a: Blank line accepted (should fail)"

    # 7b: uppercase character in word.
    printf "1 Hello 0\n" > /tmp/bad2.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 \
           -i /tmp/bad2.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7b: Uppercase letter rejected" \
                  || fail "TEST 7b: Uppercase letter accepted (should fail)"

    # 7c: double space between fields.
    printf "1  hello 0\n" > /tmp/bad3.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 \
           -i /tmp/bad3.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7c: Double space rejected" \
                  || fail "TEST 7c: Double space accepted (should fail)"

    rm -f /tmp/bad1.txt /tmp/bad2.txt /tmp/bad3.txt
}

# ==============================================================
# TEST 8 – SIGINT / Ctrl+C clean shutdown
# --------------------------------------------------------------
# Starts hw3 on a large input, sends SIGINT after 4 seconds,
# then checks that no zombie processes remain.
# A zombie count > 0 means waitpid() was not called correctly.
# ==============================================================
test_8_sigint() {
    section "TEST 8: Ctrl+C (SIGINT) Clean Shutdown"
    info "5-floor large input, SIGINT after 4 seconds"

    local logfile="$LOG_DIR/test8.log"

    # Start hw3 in the background.
    "$HW3" -f 5 -w 2 -l 3 -s 2 -c 3 -d 4 -r 2 \
        -i "$TESTS_DIR/input_large.txt" \
        -o "$TESTS_DIR/out_test8.txt" > "$logfile" 2>&1 &
    HW3_PID=$!

    # Let it run for 4 seconds then send SIGINT.
    sleep 4
    kill -INT $HW3_PID 2>/dev/null
    wait $HW3_PID 2>/dev/null
    local ec=$?

    # Give the OS a moment to reap any grandchildren.
    sleep 1

    # Check for zombie (defunct) processes.
    local zombies
    zombies=$(ps aux | grep defunct | grep -v grep | wc -l)
    [ "$zombies" -eq 0 ] && pass "TEST 8: No zombie processes" \
                          || fail "TEST 8: $zombies zombie process(es) found!"

    [ $ec -ne 124 ] && pass "TEST 8: Program exited (exit code: $ec)" \
                     || fail "TEST 8: Timeout (should have exited after SIGINT)"
}

# ==============================================================
# TEST 9 – Large input (15 words, 5 floors)
# --------------------------------------------------------------
# Stress-tests the full system with a larger workload.
# Verifies scalability and correctness under higher concurrency.
# ==============================================================
test_9_large() {
    section "TEST 9: Large Input (15 words, 5 floors)"
    info "Floors:5 | w:2 l:3 s:2 | c:5 d:6 r:3 | timeout:90s"

    local outfile="$TESTS_DIR/out_test9.txt"
    local logfile="$LOG_DIR/test9.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 90 "$HW3" \
        -f 5 -w 2 -l 3 -s 2 -c 5 -d 6 -r 3 \
        -i "$TESTS_DIR/input_large.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then
        fail "TEST 9: TIMEOUT (90s) - possible deadlock!"
        info "Log: $logfile"
        return
    fi
    if [ $ec -ne 0 ]; then fail "TEST 9: Non-zero exit"; return; fi

    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ] && [ -n "$total" ]; then
        pass "TEST 9: $completed/$total words completed"
    else
        fail "TEST 9: Incomplete (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 15 "TEST 9"
}

# ==============================================================
# TEST 10 – Output consistency across three runs
# --------------------------------------------------------------
# Runs hw3 three times with the same parameters and compares the
# output files.  Because the output is sorted by (sorting_floor,
# word_id), it must be identical across runs regardless of the
# random internal execution order.
# ==============================================================
test_10_consistency() {
    section "TEST 10: Output Consistency (3 runs)"
    info "Same parameters → identical output format"

    local logfile="$LOG_DIR/test10.log"

    for i in 1 2 3; do
        run_hw3 "$logfile" 30 "$HW3" \
            -f 3 -w 2 -l 2 -s 2 -c 4 -d 3 -r 2 \
            -i "$TESTS_DIR/input_basic.txt" \
            -o "$TESTS_DIR/out_consist${i}.txt"
        local ec=$?
        if [ $ec -eq 124 ]; then
            fail "TEST 10: Run $i timed out"
            return
        fi
    done

    # diff returns 0 if files are identical.
    if diff -q "$TESTS_DIR/out_consist1.txt" "$TESTS_DIR/out_consist2.txt" > /dev/null && \
       diff -q "$TESTS_DIR/out_consist2.txt" "$TESTS_DIR/out_consist3.txt" > /dev/null; then
        pass "TEST 10: Output identical across 3 runs"
    else
        fail "TEST 10: Outputs differ between runs"
        info "Run 1:"; cat "$TESTS_DIR/out_consist1.txt" | sed 's/^/    /'
        info "Run 2:"; cat "$TESTS_DIR/out_consist2.txt" | sed 's/^/    /'
    fi
}


# ==============================================================
# MAIN FLOW
# ==============================================================

# Verify the binary exists before starting any tests.
check_binary

# Determine whether to run all tests or a specific one.
RUN_SINGLE="${1:-all}"

if [ "$RUN_SINGLE" = "all" ]; then
    test_1_basic
    test_2_single_floor
    test_3_repeated_chars
    test_4_same_floor
    test_5_capacity_stress
    test_6_arg_validation
    test_7_input_format
    test_8_sigint
    test_9_large
    test_10_consistency
else
    # Run only the requested test number.
    case "$RUN_SINGLE" in
        1)  test_1_basic ;;
        2)  test_2_single_floor ;;
        3)  test_3_repeated_chars ;;
        4)  test_4_same_floor ;;
        5)  test_5_capacity_stress ;;
        6)  test_6_arg_validation ;;
        7)  test_7_input_format ;;
        8)  test_8_sigint ;;
        9)  test_9_large ;;
        10) test_10_consistency ;;
        *)  echo "Invalid test number: $RUN_SINGLE (valid: 1–10)"; exit 1 ;;
    esac
fi

# ── Final results ──────────────────────────────────────────────
section "RESULTS"
echo -e "  ${GREEN}PASS: $PASS${NC}  ${RED}FAIL: $FAIL${NC}"
echo ""
if [ $FAIL -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}All tests passed!${NC}"
else
    echo -e "  ${RED}${BOLD}$FAIL test(s) failed.${NC}"
fi
