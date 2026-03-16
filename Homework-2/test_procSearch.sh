#!/bin/bash
# =============================================================================
# CSE344 HW2 — procSearch Kapsamlı Test Scripti
# Kullanım: ./test_procSearch.sh
# Gereksinim: procSearch binary'si bu script ile aynı klasörde olmalı
# =============================================================================

# ── Renkler ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ── Sayaçlar ─────────────────────────────────────────────────────────────────
PASS=0
FAIL=0
WARN=0

# ── Yardımcı fonksiyonlar ─────────────────────────────────────────────────────
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

# ── Binary kontrolü ───────────────────────────────────────────────────────────
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

# Önceki test kalıntılarını temizle
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Ödevin zorunlu test senaryosu — exact olarak ödevdeki gibi
mkdir -p "$TEST_DIR/alpha/sub1"
mkdir -p "$TEST_DIR/beta"
mkdir -p "$TEST_DIR/gamma"
mkdir -p "$TEST_DIR/delta"

# Dosyaları oluştur — ödevdeki içeriklerle
echo "quarterly report data"  > "$TEST_DIR/alpha/report.txt"
touch                            "$TEST_DIR/alpha/notes.md"
echo "old duplicate report"   > "$TEST_DIR/alpha/sub1/repoort.txt"
touch                            "$TEST_DIR/alpha/sub1/image.png"
touch                            "$TEST_DIR/beta/error_log.txt"
echo "final version"          > "$TEST_DIR/beta/report_final.txt"
echo "archived report"        > "$TEST_DIR/gamma/repoooort.txt"
touch                            "$TEST_DIR/gamma/data.csv"
touch                            "$TEST_DIR/delta/summary.txt"

info "Test dizini oluşturuldu: $TEST_DIR"
info "Dizin yapısı:"
find "$TEST_DIR" | sort | sed 's|/tmp/procSearch_test||' | sed 's|^|    |'

# =============================================================================
# BÖLÜM 1: ARGÜMAN KONTROLÜ
# =============================================================================
header "BÖLÜM 1: ARGÜMAN KONTROLÜ"

# 1.1 Hiç argüman verilmezse
output=$("$BINARY" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.1 Argümansız çalıştırma → Usage mesajı gösteriliyor"
else
    fail "1.1 Argümansız çalıştırma → Usage mesajı yok"
fi

# 1.2 Sadece -d verilirse
output=$("$BINARY" -d "$TEST_DIR" 2>&1)
if echo "$output" | grep -qi "usage"; then
    pass "1.2 Eksik argüman (-d tek başına) → Usage mesajı"
else
    fail "1.2 Eksik argüman (-d tek başına) → Usage mesajı yok"
fi

# 1.3 -n aralık dışı (1)
output=$("$BINARY" -d "$TEST_DIR" -n 1 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.3 -n 1 (2'den küçük) → Hata mesajı"
else
    fail "1.3 -n 1 (2'den küçük) → Hata mesajı yok"
fi

# 1.4 -n aralık dışı (9)
output=$("$BINARY" -d "$TEST_DIR" -n 9 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "error\|usage"; then
    pass "1.4 -n 9 (8'den büyük) → Hata mesajı"
else
    fail "1.4 -n 9 (8'den büyük) → Hata mesajı yok"
fi

# 1.5 Var olmayan dizin
output=$("$BINARY" -d "/tmp/klasor_yok_12345" -n 3 -f "rep+ort" 2>&1)
if [ $? -ne 0 ] || echo "$output" | grep -qi "error\|cannot\|no such"; then
    pass "1.5 Var olmayan dizin → Hata mesajı"
else
    fail "1.5 Var olmayan dizin → Hata yok"
fi

# =============================================================================
# BÖLÜM 2: PATTERN MATCHING
# =============================================================================
header "BÖLÜM 2: PATTERN MATCHING"

# 2.1 Temel + operatörü — 4 dosya bulunmalı
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:")

if [ "$match_count" -eq 4 ]; then
    pass "2.1 rep+ort → 4 eşleşme bulundu (report, repoort, repooort, report_final)"
else
    fail "2.1 rep+ort → Beklenen 4, bulunan $match_count"
fi

# 2.2 report.txt bulunuyor mu?
if echo "$output" | grep -q "report.txt"; then
    pass "2.2 report.txt bulundu"
else
    fail "2.2 report.txt bulunamadı"
fi

# 2.3 repoort.txt bulunuyor mu?
if echo "$output" | grep -q "repoort.txt"; then
    pass "2.3 repoort.txt bulundu"
else
    fail "2.3 repoort.txt bulunamadı"
fi

# 2.4 repoooort.txt bulunuyor mu?
if echo "$output" | grep -q "repoooort.txt"; then
    pass "2.4 repoooort.txt bulundu"
else
    fail "2.4 repoooort.txt bulunamadı"
fi

# 2.5 report_final.txt bulunuyor mu?
if echo "$output" | grep -q "report_final.txt"; then
    pass "2.5 report_final.txt bulundu"
else
    fail "2.5 report_final.txt bulunamadı"
fi

# 2.6 Eşleşmeyen dosyalar çıkmamalı (notes.md, image.png vs.)
if echo "$output" | grep "MATCH:" | grep -qE "notes\.md|image\.png|error_log|data\.csv|summary"; then
    fail "2.6 Eşleşmeyen dosyalar sonuçta görünüyor"
else
    pass "2.6 Eşleşmeyen dosyalar sonuçta yok"
fi

# 2.7 Hiç eşleşmeyen pattern
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output" | grep -q "No matching files found"; then
    pass "2.7 Eşleşmeyen pattern → 'No matching files found'"
else
    fail "2.7 Eşleşmeyen pattern → 'No matching files found' mesajı yok"
fi

# 2.8 Exact match (+ yok)
mkdir -p "$TEST_DIR/extra"
touch "$TEST_DIR/extra/notes.txt"
touch "$TEST_DIR/extra/notesX.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "notes" 2>/dev/null)
if echo "$output" | grep "MATCH:" | grep -q "notes.txt" && \
   ! echo "$output" | grep "MATCH:" | grep -q "notesX"; then
    pass "2.8 Exact match 'notes' → sadece notes.txt, notesX.txt değil"
else
    warn "2.8 Exact match testi belirsiz — manuel kontrol gerekli"
fi

# 2.9 Case-insensitive test
mkdir -p "$TEST_DIR/casetest"
echo "test" > "$TEST_DIR/casetest/REPORT.txt"
echo "test" > "$TEST_DIR/casetest/Report.txt"
output=$("$BINARY" -d "$TEST_DIR" -n 2 -f "rep+ort" 2>/dev/null)
if echo "$output" | grep -q "REPORT.txt" && echo "$output" | grep -q "Report.txt"; then
    pass "2.9 Case-insensitive → REPORT.txt ve Report.txt bulundu"
else
    warn "2.9 Case-insensitive → eksik eşleşme olabilir, manuel kontrol et"
fi
rm -rf "$TEST_DIR/casetest" "$TEST_DIR/extra"

# =============================================================================
# BÖLÜM 3: BOYUT FİLTRESİ (-s)
# =============================================================================
header "BÖLÜM 3: BOYUT FİLTRESİ (-s)"

# 3.1 -s 15 ile test — "quarterly report data" = 22 byte, "final version" = 14 byte
# report.txt (22B) → geçer, report_final.txt (13B) → geçmez
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:")

# report.txt(22), repoort.txt(20), repoooort.txt(16) geçmeli
# report_final.txt(13) geçmemeli
if echo "$output" | grep "MATCH:" | grep -q "report.txt" && \
   ! echo "$output" | grep "MATCH:" | grep -q "report_final.txt"; then
    pass "3.1 -s 15: report.txt (22B) bulundu, report_final.txt (13B) filtrelendi"
else
    fail "3.1 -s 15 boyut filtresi doğru çalışmıyor"
fi

# 3.2 Çok büyük boyut — hiç eşleşme olmamalı
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 99999 2>/dev/null)
if echo "$output" | grep -q "No matching files found"; then
    pass "3.2 -s 99999 → hiç eşleşme yok"
else
    fail "3.2 -s 99999 → 'No matching files found' bekleniyor"
fi

# 3.3 -s 0 verilince filtre yok — hepsi gelmeli
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 0 2>/dev/null)
match_count=$(echo "$output" | grep -c "MATCH:")
if [ "$match_count" -ge 4 ]; then
    pass "3.3 -s 0 → filtre yok, tüm eşleşmeler geliyor ($match_count adet)"
else
    fail "3.3 -s 0 → $match_count eşleşme, 4 bekleniyordu"
fi

# =============================================================================
# BÖLÜM 4: WORKER PROCESS YÖNETİMİ
# =============================================================================
header "BÖLÜM 4: WORKER PROCESS YÖNETİMİ"

# 4.1 Worker PID formatı
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
if echo "$output" | grep -qE "\[Worker PID:[0-9]+\] MATCH:"; then
    pass "4.1 Worker PID formatı doğru: [Worker PID:XXXX] MATCH:"
else
    fail "4.1 Worker PID formatı yanlış — beklenen: [Worker PID:XXXX] MATCH:"
fi

# 4.2 Byte bilgisi var mı?
if echo "$output" | grep -qE "MATCH:.*\([0-9]+ bytes\)"; then
    pass "4.2 Byte bilgisi doğru formatta: (XXX bytes)"
else
    fail "4.2 Byte bilgisi yok veya format yanlış"
fi

# 4.3 Klasör sayısı < worker sayısı — Notice mesajı
# 4 klasör var, 6 worker istiyoruz
output=$("$BINARY" -d "$TEST_DIR" -n 6 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*only.*subdirector.*found.*using.*instead"; then
    pass "4.3 Klasör < Worker → Notice mesajı doğru"
else
    fail "4.3 Klasör < Worker → Notice mesajı yok veya format yanlış"
    info "    Beklenen: 'Notice: only N subdirectories found; using N workers instead of 6'"
    info "    Bulunan:  $(echo "$output" | grep -i notice | head -1)"
fi

# 4.4 Hiç alt klasör olmayan dizin — parent direkt arama
EMPTY_DIR="/tmp/procSearch_empty_test"
mkdir -p "$EMPTY_DIR"
echo "test report" > "$EMPTY_DIR/report.txt"
output=$("$BINARY" -d "$EMPTY_DIR" -n 2 -f "rep+ort" 2>&1)
if echo "$output" | grep -qi "notice.*no subdirector.*parent will search root"; then
    pass "4.4 Alt klasör yok → Notice + parent direkt arama"
else
    fail "4.4 Alt klasör yok → Notice mesajı yok veya format yanlış"
    info "    Beklenen: 'Notice: no subdirectories found; parent will search root directly.'"
fi
rm -rf "$EMPTY_DIR"

# 4.5 Birden fazla worker çalışıyor mu? (farklı PID'ler)
output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
pids=$(echo "$output" | grep -oE "PID:[0-9]+" | sort -u | wc -l)
if [ "$pids" -ge 2 ]; then
    pass "4.5 Birden fazla farklı Worker PID görünüyor ($pids farklı PID)"
else
    warn "4.5 Sadece $pids farklı PID — worker sayısı beklenenden az olabilir"
fi

# =============================================================================
# BÖLÜM 5: ÇIKTI FORMATI
# =============================================================================
header "BÖLÜM 5: ÇIKTI FORMATI"

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)

# 5.1 Root dizin adı yazdırılıyor mu?
if echo "$output" | grep -q "$TEST_DIR"; then
    pass "5.1 Root dizin adı çıktıda var"
else
    fail "5.1 Root dizin adı çıktıda yok"
fi

# 5.2 Ağaç formatı — |-- var mı?
if echo "$output" | grep -qE "^\|--"; then
    pass "5.2 Ağaç formatı: |-- ile başlayan satırlar var"
else
    fail "5.2 Ağaç formatı: |-- bulunamadı"
fi

# 5.3 Summary bölümü var mı?
if echo "$output" | grep -q "--- Summary ---"; then
    pass "5.3 '--- Summary ---' başlığı var"
else
    fail "5.3 '--- Summary ---' başlığı yok"
fi

# 5.4 Summary alanları
if echo "$output" | grep -q "Total workers used"; then
    pass "5.4 Summary: 'Total workers used' var"
else
    fail "5.4 Summary: 'Total workers used' yok"
fi

if echo "$output" | grep -q "Total files scanned"; then
    pass "5.5 Summary: 'Total files scanned' var"
else
    fail "5.5 Summary: 'Total files scanned' yok"
fi

if echo "$output" | grep -q "Total matches found"; then
    pass "5.6 Summary: 'Total matches found' var"
else
    fail "5.6 Summary: 'Total matches found' yok"
fi

# 5.7 Worker PID satırları summary'de var mı?
if echo "$output" | grep -qE "Worker PID [0-9]+.*: [0-9]+ match"; then
    pass "5.7 Summary: Worker PID satırları var"
else
    fail "5.7 Summary: Worker PID satırları yok"
fi

# 5.8 match/matches tekil-çoğul
if echo "$output" | grep -qE ": 1 match$"; then
    pass "5.8 Tekil: '1 match' (matches değil)"
else
    warn "5.8 Tekil match kontrolü — 1 match bulunamadı (belki tüm worker'lar 1'den fazla buldu)"
fi

# =============================================================================
# BÖLÜM 6: SİNYAL YÖNETİMİ — SIGINT (Ctrl+C)
# =============================================================================
header "BÖLÜM 6: SİNYAL YÖNETİMİ — SIGINT"

# 6.1 Büyük dizinde SIGINT testi
info "SIGINT testi başlıyor — /usr/share/doc üzerinde (3 sn sonra Ctrl+C simülasyonu)"

"$BINARY" -d /usr/share/doc -n 4 -f "read+me" > /tmp/sigint_out.txt 2>&1 &
PROC_PID=$!
sleep 2

# Program hâlâ çalışıyor mu?
if kill -0 "$PROC_PID" 2>/dev/null; then
    kill -INT "$PROC_PID"
    sleep 2   # temizliğin tamamlanması için bekle

    sigint_out=$(cat /tmp/sigint_out.txt)

    # SIGINT mesajı var mı?
    if echo "$sigint_out" | grep -q "SIGINT received"; then
        pass "6.1 SIGINT → '[Parent] SIGINT received. Terminating all workers...' mesajı var"
    else
        fail "6.1 SIGINT → mesaj yok"
        info "    Çıktı: $(echo "$sigint_out" | tail -5)"
    fi

    # Zombie kontrol
    sleep 1
    zombie_count=$(ps -ef | grep procSearch | grep -c "<defunct>" || true)
    if [ "$zombie_count" -eq 0 ]; then
        pass "6.2 SIGINT sonrası zombie process yok"
    else
        fail "6.2 SIGINT sonrası $zombie_count zombie process var!"
    fi

    # procSearch process'leri temizlendi mi?
    remaining=$(pgrep -c procSearch 2>/dev/null || echo 0)
    if [ "$remaining" -eq 0 ]; then
        pass "6.3 SIGINT sonrası tüm procSearch process'leri temizlendi"
    else
        fail "6.3 SIGINT sonrası $remaining procSearch process hâlâ çalışıyor"
        pkill -9 procSearch 2>/dev/null
    fi
else
    warn "6.1 Program SIGINT göndermeden önce bitti — daha büyük dizin gerekebilir"
fi

rm -f /tmp/sigint_out.txt

# =============================================================================
# BÖLÜM 7: ZOMBIE PROCESS KONTROLÜ
# =============================================================================
header "BÖLÜM 7: ZOMBIE PROCESS KONTROLÜ"

# 7.1 Normal çalışma sonrası zombie yok
"$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" > /dev/null 2>&1
sleep 1
zombie_count=$(ps -ef | awk '{print $8}' | grep -c "^Z$" || echo 0)
if [ "$zombie_count" -eq 0 ]; then
    pass "7.1 Normal çalışma sonrası zombie yok"
else
    fail "7.1 Normal çalışma sonrası $zombie_count zombie var!"
    ps -ef | grep defunct
fi

# =============================================================================
# BÖLÜM 8: ZORUNLU TEST SENARYOSU (Ödevden birebir)
# =============================================================================
header "BÖLÜM 8: ÖDEV'İN ZORUNLU TEST SENARYOSU"

info "Adım 1: Dizin yapısı zaten oluşturuldu (/tmp/procSearch_test)"
info "Adım 2: rep+ort pattern testi"

output=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>/dev/null)
echo ""
echo -e "${CYAN}--- Gerçek Program Çıktısı ---${NC}"
echo "$output"
echo -e "${CYAN}------------------------------${NC}"

# 4 dosya kontrol
found_report=$(echo "$output" | grep -c "report.txt" || true)
found_repoort=$(echo "$output" | grep -c "repoort.txt" || true)
found_repoooort=$(echo "$output" | grep -c "repoooort.txt" || true)
found_final=$(echo "$output" | grep -c "report_final.txt" || true)

[ "$found_report" -ge 1 ]    && pass "8.1 report.txt bulundu"    || fail "8.1 report.txt bulunamadı"
[ "$found_repoort" -ge 1 ]   && pass "8.2 repoort.txt bulundu"   || fail "8.2 repoort.txt bulunamadı"
[ "$found_repoooort" -ge 1 ] && pass "8.3 repoooort.txt bulundu" || fail "8.3 repoooort.txt bulunamadı"
[ "$found_final" -ge 1 ]     && pass "8.4 report_final.txt bulundu" || fail "8.4 report_final.txt bulunamadı"

info "Adım 3: -s 15 boyut filtresi testi"
output_s=$("$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" -s 15 2>/dev/null)
echo ""
echo -e "${CYAN}--- -s 15 Çıktısı ---${NC}"
echo "$output_s"
echo -e "${CYAN}---------------------${NC}"

info "Adım 4: Eşleşmeyen pattern testi (xyz+123)"
output_nomatch=$("$BINARY" -d "$TEST_DIR" -n 3 -f "xyz+123" 2>/dev/null)
if echo "$output_nomatch" | grep -q "No matching files found"; then
    pass "8.5 xyz+123 → 'No matching files found'"
else
    fail "8.5 xyz+123 → 'No matching files found' yok"
fi

# =============================================================================
# BÖLÜM 9: DERLEME KALİTESİ
# =============================================================================
header "BÖLÜM 9: DERLEME KALİTESİ"

# 9.1 -Wall ile 0 uyarı
warning_count=$(make -B 2>&1 | grep -c "warning:" || echo 0)
if [ "$warning_count" -eq 0 ]; then
    pass "9.1 make -Wall → 0 uyarı"
else
    fail "9.1 make -Wall → $warning_count uyarı var"
    make -B 2>&1 | grep "warning:" | head -5
fi

# 9.2 make clean çalışıyor mu?
make clean > /dev/null 2>&1
if [ ! -f "procSearch" ] && [ ! -f "main.o" ]; then
    pass "9.2 make clean → binary ve .o dosyaları silindi"
else
    fail "9.2 make clean → dosyalar silinmedi"
fi

# 9.3 make clean sonrası make tekrar çalışıyor mu?
make > /dev/null 2>&1
if [ -f "procSearch" ]; then
    pass "9.3 make clean → make → binary yeniden oluşturuldu"
else
    fail "9.3 make clean sonrası make başarısız"
fi

# =============================================================================
# BÖLÜM 10: MEMORY LEAK (Valgrind)
# =============================================================================
header "BÖLÜM 10: MEMORY LEAK (Valgrind)"

if command -v valgrind &> /dev/null; then
    valgrind_out=$(valgrind --leak-check=full --error-exitcode=1 \
        "$BINARY" -d "$TEST_DIR" -n 3 -f "rep+ort" 2>&1)
    
    if echo "$valgrind_out" | grep -q "no leaks are possible\|0 bytes in 0 blocks"; then
        pass "10.1 Valgrind: Memory leak yok"
    elif echo "$valgrind_out" | grep -q "definitely lost: 0 bytes"; then
        pass "10.1 Valgrind: Kesin memory leak yok"
    else
        warn "10.1 Valgrind: Memory leak olabilir — manuel kontrol et"
        echo "$valgrind_out" | grep -E "definitely|indirectly|possibly" | head -5
    fi
else
    warn "10.1 Valgrind yüklü değil — 'sudo apt install valgrind' ile kur"
fi

# =============================================================================
# SONUÇ
# =============================================================================
header "TEST SONUÇLARI"

TOTAL=$((PASS + FAIL + WARN))
echo ""
echo -e "  Toplam Test : ${BOLD}$TOTAL${NC}"
echo -e "  ${GREEN}Geçen       : $PASS${NC}"
echo -e "  ${RED}Başarısız   : $FAIL${NC}"
echo -e "  ${YELLOW}Uyarı       : $WARN${NC}"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}  ✓ Tüm kritik testler geçti!${NC}"
else
    echo -e "${RED}${BOLD}  ✗ $FAIL test başarısız — yukarıdaki hataları incele${NC}"
fi

echo ""
echo -e "${CYAN}  Not: SIGINT ve zombie testleri için büyük dizinlerde"
echo -e "  manuel test yapmanı da önerilir:${NC}"
echo -e "  ${BOLD}  ./procSearch -d /usr/share/doc -n 4 -f 'read+me'${NC}"
echo -e "  ${BOLD}  (çalışırken Ctrl+C bas, sonra: ps -ef | grep procSearch)${NC}"
echo ""

# Test dizinini temizle
rm -rf "$TEST_DIR"

exit $FAIL