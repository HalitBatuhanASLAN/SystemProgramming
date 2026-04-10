#!/bin/bash

# ============================================================
# CSE344 HW3 - Kapsamlı Test Script v2
# Kullanım: ./run_tests_v2.sh [test_numarası]
# Örnek:    ./run_tests_v2.sh       -> tüm testler
#           ./run_tests_v2.sh 1     -> sadece Test 1
#
# ÖNEMLİ NOT: Program çok fazla log üretiyor. Çıktılar otomatik
# olarak tests/logs/ klasörüne yönlendirilir.
# ============================================================

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
HW3="$(dirname "$TESTS_DIR")/hw3"
LOG_DIR="$TESTS_DIR/logs"
PASS=0
FAIL=0

mkdir -p "$LOG_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

section() { echo -e "\n${CYAN}${BOLD}══════════════════════════════════════════${NC}"; echo -e "${CYAN}${BOLD}  $1${NC}"; echo -e "${CYAN}${BOLD}══════════════════════════════════════════${NC}"; }
pass()    { echo -e "  ${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail()    { echo -e "  ${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info()    { echo -e "  ${YELLOW}[INFO]${NC} $1"; }

check_binary() {
    if [ ! -f "$HW3" ]; then
        echo -e "${RED}HATA: $HW3 bulunamadı. Önce 'make' çalıştır.${NC}"
        exit 1
    fi
    echo -e "${GREEN}Binary: $HW3${NC}"
    echo -e "${YELLOW}Loglar: $LOG_DIR/${NC}"
}

# hw3'ü çalıştır, çıktıyı log dosyasına yönlendir, summary'yi al
run_hw3() {
    local logfile="$1"
    local timeout_sec="$2"
    shift 2
    # Tüm çıktıyı (stdout+stderr) logfile'a yaz, terminale sadece summary
    timeout "$timeout_sec" "$@" > "$logfile" 2>&1
    return $?
}

# Logdan summary satırlarını çek
get_summary() {
    local logfile="$1"
    grep -E "Total words:|Completed words:|Retries:|Characters transported:|Delivery elevator|Reposition elevator|terminated successfully|COMPLETED" "$logfile" 2>/dev/null
}

validate_output() {
    local outfile="$1"
    local expected_count="$2"
    local test_name="$3"

    if [ ! -f "$outfile" ]; then
        fail "$test_name: Output dosyası oluşturulmadı"
        return 1
    fi

    local line_count
    line_count=$(wc -l < "$outfile")

    if [ "$line_count" -ne "$expected_count" ]; then
        fail "$test_name: Satır sayısı yanlış (beklenen: $expected_count, bulunan: $line_count)"
        return 1
    fi

    local bad_lines
    bad_lines=$(grep -cvP '^\d+ [a-z]+ \d+$' "$outfile" || true)
    if [ "$bad_lines" -gt 0 ]; then
        fail "$test_name: Format hatası ($bad_lines bozuk satır)"
        return 1
    fi

    local empty_lines
    empty_lines=$(grep -c '^$' "$outfile" 2>/dev/null || echo 0)
    if [ "$empty_lines" -gt 0 ]; then
        fail "$test_name: Boş satır var"
        return 1
    fi

    local prev_sf=-1
    local prev_wid=-1
    local sort_ok=1
    while IFS=' ' read -r wid word sf; do
        if [ "$sf" -lt "$prev_sf" ]; then sort_ok=0; break
        elif [ "$sf" -eq "$prev_sf" ] && [ "$wid" -lt "$prev_wid" ]; then sort_ok=0; break; fi
        prev_sf=$sf; prev_wid=$wid
    done < "$outfile"

    if [ "$sort_ok" -eq 0 ]; then
        fail "$test_name: Sıralama yanlış"
        return 1
    fi

    pass "$test_name: Output doğru ($line_count kelime, format+sıralama ok)"
    return 0
}

# --------------------------------------------------
test_1_basic() {
    section "TEST 1: Temel Senaryo (3 kat, 6 kelime)"
    info "Floors:3 | w:2 l:2 s:2 | c:4 d:3 r:2 | timeout:30s"

    local outfile="$TESTS_DIR/out_test1.txt"
    local logfile="$LOG_DIR/test1.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 30 "$HW3" \
        -f 3 -w 2 -l 2 -s 2 -c 4 -d 3 -r 2 \
        -i "$TESTS_DIR/input_basic.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 1: TIMEOUT (30s) - Deadlock!"; info "Log: $logfile"; return; fi
    if [ $ec -ne 0 ]; then fail "TEST 1: Hatayla çıktı (exit: $ec)"; return; fi

    if grep -q "Program terminated successfully" "$logfile"; then
        pass "TEST 1: 'Program terminated successfully'"
    else
        fail "TEST 1: Başarı mesajı yok"
    fi

    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ] && [ -n "$total" ]; then
        pass "TEST 1: Tüm kelimeler tamamlandı ($completed/$total)"
    else
        fail "TEST 1: Eksik tamamlandı (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 6 "TEST 1"
    info "Output:"; cat "$outfile" | sed 's/^/    /'
    info "Log: $logfile"
}

# --------------------------------------------------
test_2_single_floor() {
    section "TEST 2: Tek Kat"
    info "Floors:1 | w:1 l:2 s:1 | c:5 d:2 r:1 | timeout:20s"

    local outfile="$TESTS_DIR/out_test2.txt"
    local logfile="$LOG_DIR/test2.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 20 "$HW3" \
        -f 1 -w 1 -l 2 -s 1 -c 5 -d 2 -r 1 \
        -i "$TESTS_DIR/input_single_floor.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 2: TIMEOUT"; return; fi
    if [ $ec -ne 0 ]; then fail "TEST 2: Hatayla çıktı (exit: $ec)"; return; fi

    pass "TEST 2: Başarıyla tamamlandı"

    local deliv_ops
    deliv_ops=$(grep "Delivery elevator operations:" "$logfile" | grep -oP '\d+' | head -1)
    if [ -n "$deliv_ops" ] && [ "$deliv_ops" -eq 0 ]; then
        pass "TEST 2: Tek katta delivery elevator ops=0 (doğru)"
    else
        info "TEST 2: Delivery ops=$deliv_ops"
    fi

    validate_output "$outfile" 3 "TEST 2"
}

# --------------------------------------------------
test_3_repeated_chars() {
    section "TEST 3: Tekrar Eden Harfler"
    info "Floors:2 | 'mississippi', 'balloon' | timeout:30s"

    local outfile="$TESTS_DIR/out_test3.txt"
    local logfile="$LOG_DIR/test3.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 30 "$HW3" \
        -f 2 -w 2 -l 3 -s 2 -c 5 -d 4 -r 2 \
        -i "$TESTS_DIR/input_repeated_chars.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 3: TIMEOUT - Tekrar eden harflerde deadlock!"; info "Log: $logfile"; return; fi
    if [ $ec -ne 0 ]; then fail "TEST 3: Hatayla çıktı (exit: $ec)"; return; fi

    pass "TEST 3: Tamamlandı"

    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ]; then
        pass "TEST 3: $completed/$total kelime tamamlandı"
    else
        fail "TEST 3: Eksik (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 4 "TEST 3"
}

# --------------------------------------------------
test_4_same_floor() {
    section "TEST 4: Same-Floor Delivery"
    info "Floors:1 | tüm kelimeler floor 0 | timeout:20s"

    local outfile="$TESTS_DIR/out_test4.txt"
    local logfile="$LOG_DIR/test4.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 20 "$HW3" \
        -f 1 -w 1 -l 2 -s 1 -c 5 -d 2 -r 1 \
        -i "$TESTS_DIR/input_same_floor.txt" -o "$outfile"

    if grep -q "direct placement" "$logfile"; then
        pass "TEST 4: 'direct placement' log mesajı görüldü"
    else
        fail "TEST 4: 'direct placement' yok"
    fi

    validate_output "$outfile" 3 "TEST 4"
}

# --------------------------------------------------
test_5_capacity_stress() {
    section "TEST 5: Kapasite Baskısı (c:2)"
    info "Floors:3 | 10 kelime | c:2 | timeout:90s"

    local outfile="$TESTS_DIR/out_test5.txt"
    local logfile="$LOG_DIR/test5.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 90 "$HW3" \
        -f 3 -w 2 -l 3 -s 2 -c 2 -d 3 -r 2 \
        -i "$TESTS_DIR/input_capacity_stress.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 5: TIMEOUT (90s) - Deadlock!"; info "Log: $logfile"; return; fi
    if [ $ec -ne 0 ]; then fail "TEST 5: Hatayla çıktı (exit: $ec)"; return; fi

    local retries
    retries=$(grep "Retries:" "$logfile" | grep -oP '\d+' | head -1)
    info "Retry: $retries"
    if [ -n "$retries" ] && [ "$retries" -gt 0 ]; then
        pass "TEST 5: Retry mekanizması çalıştı ($retries)"
    else
        info "TEST 5: Retry=0 (kapasite yeterliydi)"
    fi

    validate_output "$outfile" 10 "TEST 5"
}

# --------------------------------------------------
test_6_arg_validation() {
    section "TEST 6: Argüman Validasyonu"
    local outfile="$TESTS_DIR/out_dummy.txt"

    "$HW3" -f 3 -w 2 -l 2 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6a: Eksik argüman reddedildi" || fail "TEST 6a: Kabul edildi"

    "$HW3" -f 0 -w 1 -l 1 -s 1 -c 1 -d 1 -r 1 -i "$TESTS_DIR/input_basic.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6b: num_floors=0 reddedildi" || fail "TEST 6b: Kabul edildi"

    "$HW3" -f 3 -w 1 -l 1 -s 1 -c 1 -d 1 -r 1 -i "/tmp/yok_xyz.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6c: Olmayan dosya reddedildi" || fail "TEST 6c: Kabul edildi"

    "$HW3" -f 3 -w 1 -l 1 -s 1 -c -1 -d 1 -r 1 -i "$TESTS_DIR/input_basic.txt" -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 6d: Negatif kapasite reddedildi" || fail "TEST 6d: Kabul edildi"
}

# --------------------------------------------------
test_7_input_format() {
    section "TEST 7: Bozuk Input Formatı"
    local outfile="$TESTS_DIR/out_dummy.txt"

    printf "1 hello 0\n\n2 world 0\n" > /tmp/bad1.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 -i /tmp/bad1.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7a: Boş satır reddedildi" || fail "TEST 7a: Kabul edildi"

    printf "1 Hello 0\n" > /tmp/bad2.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 -i /tmp/bad2.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7b: Büyük harf reddedildi" || fail "TEST 7b: Kabul edildi"

    printf "1  hello 0\n" > /tmp/bad3.txt
    "$HW3" -f 1 -w 1 -l 1 -s 1 -c 5 -d 1 -r 1 -i /tmp/bad3.txt -o "$outfile" 2>/dev/null
    [ $? -ne 0 ] && pass "TEST 7c: Çift boşluk reddedildi" || fail "TEST 7c: Kabul edildi"

    rm -f /tmp/bad1.txt /tmp/bad2.txt /tmp/bad3.txt
}

# --------------------------------------------------
test_8_sigint() {
    section "TEST 8: Ctrl+C (SIGINT)"
    info "5 katlı büyük input, 4 saniye sonra SIGINT"

    local logfile="$LOG_DIR/test8.log"

    "$HW3" -f 5 -w 2 -l 3 -s 2 -c 3 -d 4 -r 2 \
        -i "$TESTS_DIR/input_large.txt" \
        -o "$TESTS_DIR/out_test8.txt" > "$logfile" 2>&1 &
    HW3_PID=$!

    sleep 4
    kill -INT $HW3_PID 2>/dev/null
    wait $HW3_PID 2>/dev/null
    local ec=$?

    sleep 1
    local zombies
    zombies=$(ps aux | grep defunct | grep -v grep | wc -l)
    [ "$zombies" -eq 0 ] && pass "TEST 8: Zombie yok" || fail "TEST 8: $zombies zombie!"
    [ $ec -ne 124 ] && pass "TEST 8: Program kapandı (exit: $ec)" || fail "TEST 8: Timeout"
}

# --------------------------------------------------
test_9_large() {
    section "TEST 9: Büyük Input (15 kelime, 5 kat)"
    info "Floors:5 | w:2 l:3 s:2 | c:5 d:6 r:3 | timeout:90s"

    local outfile="$TESTS_DIR/out_test9.txt"
    local logfile="$LOG_DIR/test9.log"
    rm -f "$outfile"

    run_hw3 "$logfile" 90 "$HW3" \
        -f 5 -w 2 -l 3 -s 2 -c 5 -d 6 -r 3 \
        -i "$TESTS_DIR/input_large.txt" -o "$outfile"
    local ec=$?

    if [ $ec -eq 124 ]; then fail "TEST 9: TIMEOUT (90s) - Deadlock!"; info "Log: $logfile"; return; fi
    if [ $ec -ne 0 ]; then fail "TEST 9: Hatayla çıktı"; return; fi

    local total completed
    total=$(grep "Total words:" "$logfile" | grep -oP '\d+' | head -1)
    completed=$(grep "Completed words:" "$logfile" | grep -oP '\d+' | head -1)
    if [ "$total" = "$completed" ] && [ -n "$total" ]; then
        pass "TEST 9: $completed/$total kelime tamamlandı"
    else
        fail "TEST 9: Eksik (completed=$completed, total=$total)"
    fi

    validate_output "$outfile" 15 "TEST 9"
}

# --------------------------------------------------
test_10_consistency() {
    section "TEST 10: Tutarlılık (3 çalıştırma)"
    info "Aynı output => deterministik format"

    local logfile="$LOG_DIR/test10.log"

    for i in 1 2 3; do
        run_hw3 "$logfile" 30 "$HW3" \
            -f 3 -w 2 -l 2 -s 2 -c 4 -d 3 -r 2 \
            -i "$TESTS_DIR/input_basic.txt" \
            -o "$TESTS_DIR/out_consist${i}.txt"
        local ec=$?
        if [ $ec -eq 124 ]; then fail "TEST 10: Çalıştırma $i timeout"; return; fi
    done

    if diff -q "$TESTS_DIR/out_consist1.txt" "$TESTS_DIR/out_consist2.txt" > /dev/null && \
       diff -q "$TESTS_DIR/out_consist2.txt" "$TESTS_DIR/out_consist3.txt" > /dev/null; then
        pass "TEST 10: 3 çalıştırmada output aynı"
    else
        fail "TEST 10: Outputlar farklı"
        info "Out1:"; cat "$TESTS_DIR/out_consist1.txt" | sed 's/^/    /'
        info "Out2:"; cat "$TESTS_DIR/out_consist2.txt" | sed 's/^/    /'
    fi
}

# ---- ANA AKIŞ ----
check_binary

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
    case "$RUN_SINGLE" in
        1) test_1_basic ;;
        2) test_2_single_floor ;;
        3) test_3_repeated_chars ;;
        4) test_4_same_floor ;;
        5) test_5_capacity_stress ;;
        6) test_6_arg_validation ;;
        7) test_7_input_format ;;
        8) test_8_sigint ;;
        9) test_9_large ;;
        10) test_10_consistency ;;
        *) echo "Geçersiz test: $RUN_SINGLE"; exit 1 ;;
    esac
fi

section "SONUÇLAR"
echo -e "  ${GREEN}PASS: $PASS${NC}  ${RED}FAIL: $FAIL${NC}"
echo ""
[ $FAIL -eq 0 ] && echo -e "  ${GREEN}${BOLD}Tüm testler geçti! 🎉${NC}" || echo -e "  ${RED}${BOLD}$FAIL test başarısız.${NC}"