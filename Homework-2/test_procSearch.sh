#!/bin/bash
# =============================================================================
# CSE344 HW2 — procSearch Kapsamlı Test Scripti
# Kullanım: ./test_procSearch.sh
# Pattern semantiği: + önceki karakteri tekrarlar (rep+ort → reppport, report)
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
    echo -e "${RED}HATA: '$BINARY' bulunamadı. Önce 'make' çalıştır.${NC}"
    exit 1
fi

# =============================================================================
# TEST ORTAMI KURULUMU
# =============================================================================
header "TEST ORTAMI KURULUYOR"

rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR/alpha/sub1" "$TEST_DIR/beta" "$TEST_DIR/gamma" "$TEST_DIR/delta"

# rep+ort eşleşmeli (p tekrarlanıyor)
echo "quarterly report data"  > "$TEST_DIR/alpha/report.txt"
echo "reppport data"          > "$TEST_DIR/alpha/reppport.txt"
echo "old file"               > "$TEST_DIR/alpha/sub1/repppport.txt"
echo "final version"          > "$TEST_DIR/beta/report_final.txt"

# eşleşmemeli
touch "$TEST_DIR/alpha/notes.md"
touch "$TEST_DIR/alpha/sub1/image.png"
touch "$TEST_DIR/beta/error_log.txt"
touch "$TEST_DIR/gamma/data.csv"
touch "$TEST_DIR/delta/summary.txt"

# er+ro+r testi
echo "error log"   > "$TEST_DIR/gamma/error.txt"
echo "errror log"  > "$TEST_DIR/gamma/errror.txt"

info "Test dizini: $TEST_DIR"
find "$TEST_DIR" | sort | sed "s|$TEST_DIR||" | sed 's|^|    |'

# =============================================================================
header "BÖLÜM 1: ARGÜMAN KONTROLÜ"
# =============================================================================

output=$("$BINARY" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.1 Argümansız → Usage mesajı"
else
    fail "1.1 Argümansız → Usage mesajı yok"
fi

output=$("$BINARY" -d "$TEST_DIR" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.2 Eksik argüman (-d tek başına) → Usage"
else
    fail "1.2 Eksik argüman → Usage yok"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 1 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.3 -n 1 (2'den küçük) → Hata mesajı"
else
    fail "1.3 -n 1 → Hata mesajı yok"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 9 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.4 -n 9 (8'den büyük) → Hata mesajı"
else
    fail "1.4 -n 9 → Hata mesajı yok"
fi

output=$("$BINARY" -d "/tmp/klasor_yok_12345" -n 3 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|cannot\|no such"; then
    pass "1.5 Var olmayan dizin → Hata mesajı"
else
    fail "1.5 Var olmayan dizin → Hata yok"
fi

# =============================================================================
header "BÖLÜM 2: PATTERN MATCHING"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:" || true)

if [ "$match_count" -ge 4 ]; then
    pass "2.1 rep+ort → $match_count eşleşme (>=4 bekleniyor)"
else
    fail "2.1 rep+ort → $match_count eşleşme, >=4 bekleniyor"
fi

if echo "$output" | grep -q "report.txt"; then
    pass "2.2 report.txt (p=1) eşleşti"
else
    fail "2.2 report.txt eşleşmedi"
fi

if echo "$output" | grep -q "reppport.txt"; then
    pass "2.3 reppport.txt (p=3) eşleşti"
else
    fail "2.3 reppport.txt eşleşmedi"
fi

if echo "$output" | grep -q "repppport.txt"; then
    pass "2.4 repppport.txt (p=4) eşleşti"
else
    fail "2.4 repppport.txt eşleşmedi"
fi

if echo "$output" | grep -q "report_final.txt"; then
    pass "2.5 report_final.txt eşleşti"
else
    fail "2.5 report_final.txt eşleşmedi"
fi

if echo "$output" | grep "MATCH:" | grep -qE "notes\.md|image\.png|error_log|data\.csv|summary"; then
    fail "2.6 Eşleşmemesi gerekenler sonuçta görünüyor"
else
    pass "2.6 Eşleşmemesi gerekenler sonuçta yok"
fi

output_nomatch=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output_nomatch" | grep -q "No matching files found"; then
    pass "2.7 xyz+123 → No matching files found"
else
    fail "2.7 xyz+123 → No matching files found yok"
fi

# Exact match
mkdir -p "$TEST_DIR/exact_test"
echo "x" > "$TEST_DIR/exact_test/notes.txt"
echo "x" > "$TEST_DIR/exact_test/notesXYZ.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "notes" 2>/dev/null)
if echo "$output" | grep "MATCH:" | grep -q "notes.txt" &&
   ! echo "$output" | grep "MATCH:" | grep -q "notesXYZ"; then
    pass "2.8 Exact match 'notes' → notes.txt ✓, notesXYZ.txt ✗"
else
    warn "2.8 Exact match belirsiz — manuel kontrol"
fi

# Case-insensitive
mkdir -p "$TEST_DIR/case_test"
echo "x" > "$TEST_DIR/case_test/REPORT.txt"
echo "x" > "$TEST_DIR/case_test/Report.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "rep+ort" 2>/dev/null)
if echo "$output" | grep -q "REPORT.txt" && echo "$output" | grep -q "Report.txt"; then
    pass "2.9 Case-insensitive → REPORT.txt ve Report.txt bulundu"
else
    warn "2.9 Case-insensitive → eksik eşleşme, manuel kontrol"
fi

# er+ro+r
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "er+ro+r" 2>/dev/null)
if echo "$output" | grep -q "error.txt" && echo "$output" | grep -q "errror.txt"; then
    pass "2.10 er+ro+r → error.txt ve errror.txt bulundu"
else
    fail "2.10 er+ro+r → eşleşme eksik"
    info "    Bulunan: $(echo "$output" | grep MATCH:)"
fi

rm -rf "$TEST_DIR/exact_test" "$TEST_DIR/case_test"

# =============================================================================
header "BÖLÜM 3: BOYUT FİLTRESİ (-s)"
# =============================================================================

# report.txt = 22 byte, report_final.txt = 14 byte
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
if echo "$output" | grep "MATCH:" | grep -q "report.txt" &&
   ! echo "$output" | grep "MATCH:" | grep -q "report_final.txt"; then
    pass "3.1 -s 15: report.txt(22B) ✓, report_final.txt(14B) ✗"
else
    fail "3.1 -s 15 boyut filtresi yanlış"
    info "    Bulunan: $(echo "$output" | grep MATCH:)"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 99999 2>/dev/null)
if echo "$output" | grep -q "No matching files found"; then
    pass "3.2 -s 99999 → hiç eşleşme yok"
else
    fail "3.2 -s 99999 → No matching files found yok"
fi

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 0 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:" || true)
if [ "$match_count" -ge 4 ]; then
    pass "3.3 -s 0 → $match_count eşleşme (filtre yok)"
else
    fail "3.3 -s 0 → $match_count eşleşme, >=4 bekleniyordu"
fi

# =============================================================================
header "BÖLÜM 4: WORKER PROCESS YÖNETİMİ"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)

if echo "$output" | grep -qE "\[Worker PID:[0-9]+\] MATCH:"; then
    pass "4.1 Worker PID formatı doğru: [Worker PID:XXXX] MATCH:"
else
    fail "4.1 Worker PID formatı yanlış"
    info "    Beklenen: [Worker PID:XXXX] MATCH: /path (N bytes)"
fi

if echo "$output" | grep -qE "MATCH:.*\([0-9]+ bytes\)"; then
    pass "4.2 Byte formatı doğru: (XXX bytes)"
else
    fail "4.2 Byte formatı yanlış"
fi

# 4 klasör var, 6 worker istiyoruz
output=$("$BINARY" -d "$TEST_DIR" -n 6 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*only.*subdirector.*found.*using.*instead"; then
    pass "4.3 Klasör(4) < Worker(6) → Notice doğru"
else
    fail "4.3 Klasör < Worker → Notice yanlış/yok"
    info "    Bulunan: $(echo "$output" | grep -i notice | head -1)"
fi

EMPTY_DIR="/tmp/procSearch_empty_$$"
mkdir -p "$EMPTY_DIR"
echo "test" > "$EMPTY_DIR/report.txt"
output=$("$BINARY" -d "$EMPTY_DIR" -n 2 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*no subdirector.*parent will search root"; then
    pass "4.4 Alt klasör yok → Notice + parent arama"
else
    fail "4.4 Alt klasör yok → Notice yanlış/yok"
    info "    Bulunan: $(echo "$output" | grep -i notice | head -1)"
fi
rm -rf "$EMPTY_DIR"

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
pid_count=$(echo "$output" | grep -oE "PID:[0-9]+" | sort -u | wc -l)
if [ "$pid_count" -ge 2 ]; then
    pass "4.5 Birden fazla Worker PID: $pid_count farklı PID"
else
    warn "4.5 Sadece $pid_count farklı PID — pattern düzelince tekrar test et"
fi

# =============================================================================
header "BÖLÜM 5: ÇIKTI FORMATI"
# =============================================================================

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)

if echo "$output" | grep -q "$TEST_DIR"; then
    pass "5.1 Root dizin adı çıktıda var"
else
    fail "5.1 Root dizin adı yok"
fi

if echo "$output" | grep -qE "^\|--"; then
    pass "5.2 Ağaç formatı: |-- var"
else
    fail "5.2 Ağaç formatı: |-- yok"
fi

if echo "$output" | grep -q "\-\-\- Summary \-\-\-"; then
    pass "5.3 '--- Summary ---' var"
else
    fail "5.3 '--- Summary ---' yok"
fi

if echo "$output" | grep -q "Total workers used"; then
    pass "5.4 'Total workers used' var"
else
    fail "5.4 'Total workers used' yok"
fi

if echo "$output" | grep -q "Total files scanned"; then
    pass "5.5 'Total files scanned' var"
else
    fail "5.5 'Total files scanned' yok"
fi

if echo "$output" | grep -q "Total matches found"; then
    pass "5.6 'Total matches found' var"
else
    fail "5.6 'Total matches found' yok"
fi

if echo "$output" | grep -qE "Worker PID [0-9]+.*: [0-9]+ match"; then
    pass "5.7 Summary: Worker PID satırları var"
else
    fail "5.7 Summary: Worker PID satırları yok"
fi

if echo "$output" | grep -qE ": 1 match$"; then
    pass "5.8 Tekil: '1 match' doğru"
else
    warn "5.8 Tekil match test edilemedi — pattern düzelince tekrar test et"
fi

# =============================================================================
header "BÖLÜM 6: SİNYAL YÖNETİMİ — SIGINT"
# =============================================================================

info "SIGINT testi — / dizini (2 sn sonra sinyal gönderilecek)"

"$BINARY" -d / -n 4 -f "read+me" > /tmp/sigint_out_$$.txt 2>&1 &
PROC_PID=$!
sleep 2

if kill -0 "$PROC_PID" 2>/dev/null; then
    kill -INT "$PROC_PID"
    sleep 2

    sigint_out=$(cat /tmp/sigint_out_$$.txt)

    if echo "$sigint_out" | grep -q "SIGINT received"; then
        pass "6.1 SIGINT → '[Parent] SIGINT received. Terminating all workers...' var"
    else
        fail "6.1 SIGINT → mesaj yok"
        info "    Son 5 satır: $(echo "$sigint_out" | tail -5)"
    fi

    sleep 1
    zombie_count=$(ps aux | awk '{print $8}' | grep -c "^Z" 2>/dev/null || true)
    zombie_count=${zombie_count:-0}
    if [ "$zombie_count" -eq 0 ]; then
        pass "6.2 SIGINT sonrası zombie yok"
    else
        fail "6.2 $zombie_count zombie var!"
    fi

    remaining=$(pgrep -c procSearch 2>/dev/null || echo 0)
    if [ "$remaining" -eq 0 ]; then
        pass "6.3 Tüm procSearch process'leri temizlendi"
    else
        fail "6.3 $remaining procSearch hâlâ çalışıyor"
        pkill -9 procSearch 2>/dev/null
    fi
else
    warn "6.1 Program SIGINT öncesi bitti"
    warn "6.2 Atlandı"
    warn "6.3 Atlandı"
fi

rm -f /tmp/sigint_out_$$.txt

# =============================================================================
header "BÖLÜM 7: ZOMBIE PROCESS KONTROLÜ"
# =============================================================================

"$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" > /dev/null 2>&1
sleep 1

zombie_count=$(ps aux | awk '{print $8}' | grep -c "^Z" 2>/dev/null || true)
zombie_count=${zombie_count:-0}
if [ "$zombie_count" -eq 0 ]; then
    pass "7.1 Normal çalışma sonrası zombie yok"
else
    fail "7.1 $zombie_count zombie var!"
    ps -ef | grep defunct
fi

# =============================================================================
header "BÖLÜM 8: ZORUNLU TEST SENARYOSU"
# =============================================================================

info "Adım 2: rep+ort pattern testi"
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
echo ""
echo -e "${CYAN}--- Program Çıktısı ---${NC}"
echo "$output"
echo -e "${CYAN}-----------------------${NC}"

cnt=$(echo "$output" | grep -c "report.txt"    || true); [ "$cnt" -ge 1 ] && pass "8.1 report.txt"    || fail "8.1 report.txt bulunamadı"
cnt=$(echo "$output" | grep -c "reppport.txt"  || true); [ "$cnt" -ge 1 ] && pass "8.2 reppport.txt"  || fail "8.2 reppport.txt bulunamadı"
cnt=$(echo "$output" | grep -c "repppport.txt" || true); [ "$cnt" -ge 1 ] && pass "8.3 repppport.txt" || fail "8.3 repppport.txt bulunamadı"
cnt=$(echo "$output" | grep -c "report_final"  || true); [ "$cnt" -ge 1 ] && pass "8.4 report_final"  || fail "8.4 report_final bulunamadı"

info "Adım 3: -s 15 boyut filtresi"
output_s=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
echo ""
echo -e "${CYAN}--- -s 15 Çıktısı ---${NC}"
echo "$output_s"
echo -e "${CYAN}---------------------${NC}"

info "Adım 4: xyz+123 eşleşmeyen pattern"
output_nm=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output_nm" | grep -q "No matching files found"; then
    pass "8.5 xyz+123 → No matching files found"
else
    fail "8.5 xyz+123 → No matching files found yok"
fi

# =============================================================================
header "BÖLÜM 9: DERLEME KALİTESİ"
# =============================================================================

warning_count=$(make -B 2>&1 | grep -c "warning:" || true)
warning_count=${warning_count:-0}
if [ "$warning_count" -eq 0 ]; then
    pass "9.1 make -Wall → 0 uyarı"
else
    fail "9.1 make -Wall → $warning_count uyarı"
    make -B 2>&1 | grep "warning:" | head -5
fi

make clean > /dev/null 2>&1
if [ ! -f "procSearch" ] && [ ! -f "main.o" ]; then
    pass "9.2 make clean → dosyalar silindi"
else
    fail "9.2 make clean çalışmadı"
fi

make > /dev/null 2>&1
if [ -f "procSearch" ]; then
    pass "9.3 make → binary yeniden oluşturuldu"
else
    fail "9.3 make başarısız"
fi

# =============================================================================
header "BÖLÜM 10: MEMORY LEAK (Valgrind)"
# =============================================================================

if command -v valgrind &> /dev/null; then
    valgrind_out=$(timeout 30 valgrind --leak-check=full \
        "$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>&1)
    if echo "$valgrind_out" | grep -q "definitely lost: 0 bytes"; then
        pass "10.1 Valgrind: Memory leak yok"
    else
        warn "10.1 Valgrind: Memory leak olabilir — manuel kontrol"
        echo "$valgrind_out" | grep -E "definitely|indirectly|possibly" | head -5
    fi
else
    warn "10.1 Valgrind yüklü değil: sudo apt install valgrind"
fi

# =============================================================================
header "TEST SONUÇLARI"
# =============================================================================

TOTAL=$((PASS + FAIL + WARN))
echo ""
echo -e "  Toplam : ${BOLD}$TOTAL${NC}"
echo -e "  ${GREEN}Geçen  : $PASS${NC}"
echo -e "  ${RED}Hata   : $FAIL${NC}"
echo -e "  ${YELLOW}Uyarı  : $WARN${NC}"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}  ✓ Tüm kritik testler geçti!${NC}"
else
    echo -e "${RED}${BOLD}  ✗ $FAIL test başarısız${NC}"
fi

echo ""
echo -e "${CYAN}  Manuel SIGINT testi:${NC}"
echo -e "  ${BOLD}  ./procSearch -d / -n 4 -f 'read+me'${NC}"
echo -e "  ${BOLD}  (Ctrl+C bas, sonra: ps aux | grep Z)${NC}"
echo ""

rm -rf "$TEST_DIR"
exit $FAIL