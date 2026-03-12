#!/bin/bash

PASS=0
FAIL=0

check() {
    local test_name="$1"
    local expected="$2"
    local actual="$3"

    if echo "$actual" | grep -q "$expected"; then
        echo "✓ $test_name"
        PASS=$((PASS + 1))
    else
        echo "✗ $test_name"
        echo "  Expected : $expected"
        echo "  Actual   : $actual"
        FAIL=$((FAIL + 1))
    fi
}

pause() {
    echo ""
    echo "--- Press ENTER to continue ---"
    read
}

clear
echo "========================================="
echo "          TEST STARTING"
echo "========================================="
pause

# ── GROUP 1 ──
clear
echo "--- GROUP 1: Invalid Usage ---"
check "1.1 No parameters"              "Error" "$(./HBA_file_search 2>&1)"
check "1.2 Only -w, no criteria"       "Error" "$(./HBA_file_search -w /etc 2>&1)"
check "1.3 -w missing"                 "Error" "$(./HBA_file_search -f "passwd" 2>&1)"
# 1.4 → prints "Invalid file type"
check "1.4 Invalid file type -t z"     "Invalid file type"   "$(./HBA_file_search -w /etc -t z 2>&1)"

# 1.5 → prints "Invalid permissions"
check "1.5 Invalid permission 8 chars" "Invalid permissions"  "$(./HBA_file_search -w /etc -p "rwxr-xr-" 2>&1)"

# 1.6 → prints "Invalid permissions"
check "1.6 Invalid permission char q"  "Invalid permissions"  "$(./HBA_file_search -w /etc -p "rwxr-xr-q" 2>&1)"

# 1.8 → prints "invalid option"
check "1.8 Unknown parameter -z"       "invalid option"       "$(./HBA_file_search -w /etc -z 2>&1)"
pause

# ── GROUP 2 ──
clear
echo "--- GROUP 2: File Name Search ---"
check "2.1 Exact match passwd"             "passwd"      "$(./HBA_file_search -w /etc -f "passwd" 2>/dev/null)"
check "2.2 Uppercase PASSWD"               "passwd"      "$(./HBA_file_search -w /etc -f "PASSWD" 2>/dev/null)"
check "2.3 Mixed case PaSsWd"              "passwd"      "$(./HBA_file_search -w /etc -f "PaSsWd" 2>/dev/null)"
check "2.4 Partial match pass"             "passwd"      "$(./HBA_file_search -w /etc -f "pass" 2>/dev/null)"
check "2.5 + operator pas+wd"              "passwd"      "$(./HBA_file_search -w /etc -f "pas+wd" 2>/dev/null)"
check "2.6 + operator p+asswd"             "passwd"      "$(./HBA_file_search -w /etc -f "p+asswd" 2>/dev/null)"
check "2.7 Multiple + pa+s+wd"             "passwd"      "$(./HBA_file_search -w /etc -f "pa+s+wd" 2>/dev/null)"
check "2.8 No match xyzabc123"             "No file found" "$(./HBA_file_search -w /etc -f "xyzabc123" 2>/dev/null)"
check "2.9 Single character x"             "|---"        "$(./HBA_file_search -w /etc -f "x" 2>/dev/null)"
check "2.10 conf extension files"          "conf"        "$(./HBA_file_search -w /etc -f "conf" 2>/dev/null)"
pause

# ── GROUP 3 ──
clear
echo "--- GROUP 3: File Type Search ---"
check "3.1 Regular files -t f"           "|---"        "$(./HBA_file_search -w /etc -t f 2>/dev/null)"
check "3.2 Directories -t d"             "|---"        "$(./HBA_file_search -w /etc -t d 2>/dev/null)"
check "3.3 Symbolic links -t l"          "/etc"        "$(./HBA_file_search -w /etc -t l 2>/dev/null)"
check "3.4 Pipe -t p"                    "/etc"        "$(./HBA_file_search -w /etc -t p 2>/dev/null)"
check "3.5 Socket -t s"                  "/etc"        "$(./HBA_file_search -w /etc -t s 2>/dev/null)"
pause

# ── GROUP 4 ──
clear
echo "--- GROUP 4: Size Search ---"
check "4.1 Empty files -b 0"             "|---"        "$(./HBA_file_search -w /etc -b 0 2>/dev/null)"
check "4.2 Small size -b 33"             "/etc"        "$(./HBA_file_search -w /etc -b 33 2>/dev/null)"
check "4.3 Large size -b 102400"         "/etc"        "$(./HBA_file_search -w /etc -b 102400 2>/dev/null)"
check "4.4 Non-existent size -b 99999999" "No file found" "$(./HBA_file_search -w /etc -b 99999999 2>/dev/null)"
pause

# ── GROUP 5 ──
clear
echo "--- GROUP 5: Permission Search ---"
check "5.1 rw-r--r-- permission"         "|---"        "$(./HBA_file_search -w /etc -p "rw-r--r--" 2>/dev/null)"
check "5.2 rwxr-xr-x permission"         "|---"        "$(./HBA_file_search -w /etc -p "rwxr-xr-x" 2>/dev/null)"
check "5.3 rw------- permission"         "/etc"        "$(./HBA_file_search -w /etc -p "rw-------" 2>/dev/null)"
check "5.4 rwxrwxrwx missing"            "No file found" "$(./HBA_file_search -w /etc -p "rwxrwxrwx" -t f -t f 2>/dev/null)"
pause

# ── GROUP 6 ──
clear
echo "--- GROUP 6: Link Count Search ---"
check "6.1 Link count 1"                 "|---"        "$(./HBA_file_search -w /etc -l 1 2>/dev/null)"
check "6.2 Link count 4"                 "/etc"        "$(./HBA_file_search -w /etc -l 4 2>/dev/null)"
check "6.3 Non-existent link count 999"  "No file found" "$(./HBA_file_search -w /etc -l 999 2>/dev/null)"
pause

# ── GROUP 7 ──
clear
echo "--- GROUP 7: Multiple Criteria ---"
check "7.1 Name + type"                  "passwd"      "$(./HBA_file_search -w /etc -f "passwd" -t f 2>/dev/null)"
check "7.2 Name + size"                  "/etc"        "$(./HBA_file_search -w /etc -f "passwd" -b 0 2>/dev/null)"
check "7.3 Type + size"                  "|---"        "$(./HBA_file_search -w /etc -t f -b 0 2>/dev/null)"
check "7.4 Name + type + permission"     "passwd"      "$(./HBA_file_search -w /etc -f "passwd" -t f -p "rw-r--r--" 2>/dev/null)"
# Get actual size first, then test with it
PASSWD_SIZE=$(stat -c%s /etc/passwd)
check "7.5 All criteria" "passwd" "$(./HBA_file_search -w /etc -f "passwd" -t f -b $PASSWD_SIZE -p "rw-r--r--" -l 1 2>/dev/null)"
check "7.6 Conflicting criteria passwd + -td" "No file found" "$(./HBA_file_search -w /etc -f "passwd" -t d 2>/dev/null)"
pause

# ── GROUP 8 ──
clear
echo "--- GROUP 8: Special Cases ---"
check "8.1 Inaccessible directory /root" "Error"       "$(./HBA_file_search -w /root -f "passwd" 2>&1)"
check "8.2 Non-existent directory"       "Error"       "$(./HBA_file_search -w /missingdir -f "passwd" 2>&1)"
mkdir -p /tmp/empty_dir
check "8.3 Empty directory"              "No file found" "$(./HBA_file_search -w /tmp/empty_dir -f "test" 2>/dev/null)"
rmdir /tmp/empty_dir
check "8.4 Deep recursive whole system"  "passwd"      "$(./HBA_file_search -w / -f "passwd" -t f 2>/dev/null)"
check "8.5 All system directories"       "|---"        "$(./HBA_file_search -w / -t d 2>/dev/null)"
pause

# ── GROUP 9 — CTRL-C and Memory Leak Test ──
echo ""
echo "--- GROUP 9: CTRL-C and Memory Leak Test ---"

# 9.1 — Normal valgrind test (without CTRL-C)
VALGRIND_OUTPUT=$(valgrind --leak-check=full ./HBA_file_search -w /etc -f "passwd" 2>&1)
check "9.1 No memory leaks in normal run" "no leaks are possible" "$VALGRIND_OUTPUT"
check "9.2 No valgrind errors in normal run" "0 errors"             "$VALGRIND_OUTPUT"

# 9.3 — CTRL-C manual test
echo ""
echo "9.3 CTRL-C test — Manual:"
echo "  Run the following command, press CTRL-C after 2 seconds:"
echo "  ./HBA_file_search -w /etc -t f"
echo "  Expected: 'Received SIGINT, exitting program ...' message should appear"
echo ""
read -p "  Did the CTRL-C message appear? (y/n): " CTRLC_RESULT
if [ "$CTRLC_RESULT" = "y" ] || [ "$CTRLC_RESULT" = "Y" ]; then
    echo "✓ 9.3 CTRL-C message appears"
    PASS=$((PASS + 1))
else
    echo "✗ 9.3 CTRL-C message does not appear"
    FAIL=$((FAIL + 1))
fi

pause
# ── Result ──
clear
echo "========================================="
echo "              Results"
echo "========================================="
echo "  Total : $((PASS + FAIL)) test"
echo "  Passed  : $PASS ✓"
echo "  Failed  : $FAIL ✗"
echo "========================================="