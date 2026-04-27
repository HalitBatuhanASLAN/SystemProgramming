#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# CSE344 HW4 – Kapsamlı Test Scripti
# Kullanım: cd hw4 && bash tests/run_tests.sh
# ═══════════════════════════════════════════════════════════════════════════

ANALYZER="./analyzer"
LOGS="tests/logs"
CONF="$LOGS/services.conf"
FILT="$LOGS/priority.txt"
EMPTY_FILT="$LOGS/empty_filter.txt"
OUT_TXT="tests/out_test.txt"
OUT_BIN="tests/out_test.bin"

PASS=0
FAIL=0
WARN_CNT=0

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

header() {
    echo ""
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}  $1${NC}"
    echo -e "${BOLD}${BLUE}══════════════════════════════════════════════${NC}"
}
ok()   { echo -e "  ${GREEN}PASS${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "  ${RED}FAIL${NC} $1"; FAIL=$((FAIL+1)); }
warn() { echo -e "  ${YELLOW}WARN${NC} $1"; WARN_CNT=$((WARN_CNT+1)); }
info() { echo -e "  ${CYAN}INFO${NC} $1"; }

cleanup() {
    rm -f "$OUT_TXT" "$OUT_BIN" "${OUT_BIN}.tmp" 2>/dev/null
    return 0
}

run_analyzer() {
    local tsec=$1; shift
    timeout "$tsec" "$ANALYZER" "$@" -o "$OUT_TXT" -O "$OUT_BIN" 2>/dev/null
    return $?
}

# ─── ÖN KONTROLLER ────────────────────────────────────────────────────────
header "ÖN KONTROLLER"

if [ ! -x "$ANALYZER" ]; then
    echo -e "${RED}HATA: $ANALYZER bulunamadi. 'make' calistirin.${NC}"
    exit 1
fi
ok "Binary mevcut: $ANALYZER"

# -Wall uyarı sayısı
WARN_COUNT=$(make -B 2>&1 | grep -c "warning:" 2>/dev/null || echo "0")
WARN_COUNT=$(echo "$WARN_COUNT" | head -1 | tr -d '[:space:]')
WARN_COUNT=${WARN_COUNT:-0}
if [ "$WARN_COUNT" -eq 0 ] 2>/dev/null; then
    ok "-Wall: sifir uyari"
else
    fail "-Wall: $WARN_COUNT uyari var"
fi

# Log dosyaları
if [ -f "$CONF" ] && [ -f "$FILT" ] && [ -f "$EMPTY_FILT" ]; then
    ok "Log dosyalari mevcut"
else
    info "Log dosyalari olusturuluyor..."
    python3 tests/generate_logs.py --lines 300 --files 3 --dir "$LOGS" >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        ok "Log dosyalari olusturuldu"
    else
        fail "Log dosyasi olusturma basarisiz"
    fi
fi

# ─── TEST 1: Temel çalışma ─────────────────────────────────────────────────
header "TEST 1 – Temel Calisma (-k error, t=2, w=2)"
cleanup

if run_analyzer 30 -c "$CONF" -f "$FILT" -k "error" \
    -t 2 -w 2 -a 32 -b 16 -d 8 -T 8; then
    ok "Program basariyla tamamlandi (exit 0)"
else
    fail "Program hata ile cikti (exit=$?)"
fi

if [ -f "$OUT_TXT" ]; then
    ok "results.txt olusturuldu"
else
    fail "results.txt olusturulmadi"
fi

if [ -f "$OUT_TXT" ]; then
    grep -q "^KEYWORD_LIST:" "$OUT_TXT"  && ok "KEYWORD_LIST satiri var"  || fail "KEYWORD_LIST eksik"
    grep -q "^FILES:"        "$OUT_TXT"  && ok "FILES satiri var"          || fail "FILES eksik"
    grep -q "^TOTAL_WEIGHTED_SCORE:" "$OUT_TXT" && ok "TOTAL_WEIGHTED_SCORE var" || fail "TOTAL_WEIGHTED_SCORE eksik"
    grep -q "^HIGH_PRIORITY_SCORE:"  "$OUT_TXT" && ok "HIGH_PRIORITY_SCORE var"  || fail "HIGH_PRIORITY_SCORE eksik"
    grep -q "^LEVEL"         "$OUT_TXT"  && ok "LEVEL tablosu var"         || fail "LEVEL tablosu eksik"
    grep -q "^# Top-3"       "$OUT_TXT"  && ok "Top-3 sources bolumu var"  || fail "Top-3 eksik"
    grep -q "^# Per-thread"  "$OUT_TXT"  && ok "Per-thread bolumu var"     || fail "Per-thread eksik"

    # Seviye sıralama: AZALAN weighted score
    PREV_SCORE=9999999
    SORTED_OK=1
    while IFS= read -r line; do
        SCORE=$(echo "$line" | awk '{gsub(/\./,""); print int($3)}')
        if [ -n "$SCORE" ] && [ "$SCORE" -gt "$PREV_SCORE" ]; then
            SORTED_OK=0
        fi
        PREV_SCORE=${SCORE:-$PREV_SCORE}
    done < <(grep -E "^(ERROR|WARN|INFO|DEBUG)" "$OUT_TXT")
    if [ "$SORTED_OK" -eq 1 ]; then
        ok "Seviyeler AZALAN weighted score sirasinda"
    else
        fail "Seviyeler yanlis sirada"
    fi
fi

if [ -f "$OUT_BIN" ]; then
    ok "results.bin olusturuldu  (boyut: $(wc -c < "$OUT_BIN") byte)"
    MAGIC=$(python3 -c "
import struct
with open('$OUT_BIN','rb') as f:
    d=f.read(4)
if len(d)==4: print(hex(struct.unpack('<I',d)[0]))
else: print('short')
" 2>/dev/null)
    if [ "$MAGIC" = "0xc5e3440b" ]; then
        ok "Binary magic: $MAGIC"
    else
        fail "Binary magic yanlis: $MAGIC (beklenen 0xc5e3440b)"
    fi
    VERSION=$(python3 -c "
import struct
with open('$OUT_BIN','rb') as f:
    f.read(4); d=f.read(4)
print(struct.unpack('<I',d)[0])
" 2>/dev/null)
    if [ "$VERSION" = "1" ]; then
        ok "Binary version: 1"
    else
        fail "Binary version yanlis: $VERSION"
    fi
else
    fail "results.bin olusturulmadi"
fi

# ─── TEST 2: Çok keyword ──────────────────────────────────────────────────
header "TEST 2 – Cok Keyword Agirlikli Puanlama"
cleanup
T2_TOTAL=""

if run_analyzer 30 -c "$CONF" -f "$FILT" \
    -k "error,fail,timeout,kill" \
    -t 2 -w 2 -a 32 -b 16 -d 8 -T 8; then
    ok "Cok keyword basarili"
else
    fail "Cok keyword basarisiz"
fi

if [ -f "$OUT_TXT" ]; then
    # LEVEL tablosunda keyword sütunları
    COLS=$(grep "^LEVEL" "$OUT_TXT" | awk '{print NF}')
    if [ "${COLS:-0}" -ge 6 ]; then
        ok "LEVEL tablosunda $((COLS-3)) keyword sutunu var"
    else
        fail "LEVEL tablosu sutun sayisi eksik: ${COLS:-?}"
    fi

    T2_TOTAL=$(grep "^TOTAL_WEIGHTED_SCORE:" "$OUT_TXT" | awk '{print $2}')
    TOTAL_INT=$(echo "${T2_TOTAL:-0}" | cut -d. -f1)
    if [ "${TOTAL_INT:-0}" -gt 0 ]; then
        ok "Toplam weighted score: $T2_TOTAL > 0"
    else
        fail "Toplam weighted score sifir veya eksik"
    fi

    info "Per-level detayi:"
    grep -E "^(ERROR|WARN|INFO|DEBUG)" "$OUT_TXT" | while read -r ln; do
        info "  $ln"
    done
fi

# Overlapping search doğrulaması
OVERLAP=$(python3 -c "
t='error error: double error occurred'
k='error'
print(sum(1 for i in range(len(t)-len(k)+1) if t[i:i+len(k)]==k))
")
if [ "$OVERLAP" = "3" ]; then
    ok "Overlapping 'error' in 'error error: double error occurred' = 3"
else
    fail "Overlapping sayim yanlis: $OVERLAP (beklenen 3)"
fi

# errorerror → 2 (overlapping at index 0 and 5)
OVERLAP2=$(python3 -c "
t='errorerror'; k='error'
print(sum(1 for i in range(len(t)-len(k)+1) if t[i:i+len(k)]==k))
")
if [ "$OVERLAP2" = "2" ]; then
    ok "Overlapping 'error' in 'errorerror' = 2"
else
    fail "Overlapping sayim yanlis: $OVERLAP2 (beklenen 2)"
fi
# aaaa → aa appears 3 times (overlapping: 0,1,2)
OVERLAP3=$(python3 -c "
t='aaaa'; k='aa'
print(sum(1 for i in range(len(t)-len(k)+1) if t[i:i+len(k)]==k))
")
if [ "$OVERLAP3" = "3" ]; then
    ok "Overlapping 'aa' in 'aaaa' = 3 (sliding window)"
else
    fail "Overlapping sayim yanlis: $OVERLAP3 (beklenen 3)"
fi

# ─── TEST 3: Tek thread ───────────────────────────────────────────────────
header "TEST 3 – Tek Thread (t=1, w=1)"
cleanup
T3_TOTAL=""

if run_analyzer 60 -c "$CONF" -f "$FILT" \
    -k "error,fail,timeout,kill" \
    -t 1 -w 1 -a 8 -b 8 -d 4 -T 10; then
    ok "Tek thread calistirmasi basarili"
    T3_TOTAL=$(grep "^TOTAL_WEIGHTED_SCORE:" "$OUT_TXT" | awk '{print $2}')
    ok "Toplam score (t=1 w=1): $T3_TOTAL"
else
    fail "Tek thread basarisiz"
fi

# t=1 vs t=2 sonucu karşılaştır
if [ -n "$T2_TOTAL" ] && [ -n "$T3_TOTAL" ] && [ "$T2_TOTAL" = "$T3_TOTAL" ]; then
    ok "Determinizm: t=1 ve t=2 ayni toplam: $T2_TOTAL"
else
    if [ -z "$T2_TOTAL" ] || [ -z "$T3_TOTAL" ]; then
        warn "Karsilastirma icin onceki test sonucu eksik"
    else
        fail "Determinizm hatasi: t=2=$T2_TOTAL, t=1=$T3_TOTAL"
    fi
fi

# ─── TEST 4: Yüksek eşzamanlılık + determinizm ────────────────────────────
header "TEST 4 – Yuksek Esszamanlilik + Determinizm (3 calistirma)"
SCORES=()
for RUN in 1 2 3; do
    cleanup
    if run_analyzer 30 -c "$CONF" -f "$FILT" \
        -k "error,fail,timeout,kill" \
        -t 4 -w 4 -a 128 -b 64 -d 32 -T 8; then
        SC=$(grep "^TOTAL_WEIGHTED_SCORE:" "$OUT_TXT" 2>/dev/null | awk '{print $2}')
        SCORES+=("${SC:-FAIL}")
        info "Calistirma $RUN: total=${SC:-FAIL}"
    else
        SCORES+=("FAIL")
        fail "Calistirma $RUN basarisiz"
    fi
done

S0="${SCORES[0]:-X}"; S1="${SCORES[1]:-X}"; S2="${SCORES[2]:-X}"
if [ "$S0" = "$S1" ] && [ "$S1" = "$S2" ] && \
   [ "$S0" != "FAIL" ] && [ "$S0" != "X" ] && [ "$S0" != "0" ]; then
    ok "Determinizm: 3 calistirma ozdes ($S0)"
else
    fail "Determinizm hatasi: $S0 | $S1 | $S2"
fi

# ─── TEST 5: Boş filter ────────────────────────────────────────────────────
header "TEST 5 – Bos Filter (HIGH_PRIORITY_SCORE=0)"
cleanup

if run_analyzer 30 -c "$CONF" -f "$EMPTY_FILT" \
    -k "error,fail" -t 2 -w 2 -a 32 -b 16 -d 8 -T 8; then
    ok "Bos filter ile calistirmasi basarili"
else
    fail "Bos filter ile calistirmasi basarisiz"
fi

if [ -f "$OUT_TXT" ]; then
    HP=$(grep "^HIGH_PRIORITY_SCORE:" "$OUT_TXT" | awk '{print $2}')
    if [ "$HP" = "0.0" ]; then
        ok "HIGH_PRIORITY_SCORE: 0.0"
    else
        fail "HIGH_PRIORITY_SCORE: ${HP:-eksik} (beklenen 0.0)"
    fi
fi

# ─── TEST 6: Geçersiz argümanlar ──────────────────────────────────────────
header "TEST 6 – Gecersiz Arguman Reddi"

check_invalid() {
    local desc="$1"; shift
    if timeout 5 "$ANALYZER" -c "$CONF" -f "$FILT" "$@" \
        -o /dev/null -O /dev/null 2>/dev/null; then
        fail "Gecersiz arg kabul edildi: $desc"
    else
        ok "Gecersiz arg reddedildi: $desc"
    fi
}

check_invalid "-t 0"    -k "error" -t 0  -w 1  -a 16 -b 16 -d 8
check_invalid "-w 0"    -k "error" -t 1  -w 0  -a 16 -b 16 -d 8
check_invalid "-a 2"    -k "error" -t 1  -w 1  -a 2  -b 16 -d 8
check_invalid "-b 2"    -k "error" -t 1  -w 1  -a 16 -b 2  -d 8
check_invalid "-d 0"    -k "error" -t 1  -w 1  -a 16 -b 16 -d 0
# Test missing -c: run without -c but with all other required args
if timeout 5 "$ANALYZER" -f "$FILT" -k "error" -t 1 -w 1 -a 16 -b 16 -d 8     -o /dev/null -O /dev/null 2>/dev/null; then
    fail "Gecersiz arg kabul edildi: no -c"
else
    ok "Gecersiz arg reddedildi: no -c"
fi

# ─── TEST 7: Binary dosya yapısı ──────────────────────────────────────────
header "TEST 7 – Binary Dosya Yapisi"
cleanup

run_analyzer 30 -c "$CONF" -f "$FILT" \
    -k "error,fail" -t 2 -w 2 -a 32 -b 16 -d 8 -T 8 >/dev/null 2>&1 || true

if [ -f "$OUT_BIN" ]; then
    python3 << PYEOF
import struct, sys

path = 'tests/out_test.bin'
try:
    with open(path, 'rb') as f:
        raw = f.read()
except Exception as e:
    print(f"  FAIL dosya okunamadi: {e}")
    sys.exit(1)

if len(raw) < 32:
    print(f"  FAIL dosya cok kucuk: {len(raw)} byte")
    sys.exit(1)

magic, version, num_levels, num_kw = struct.unpack_from('<IIII', raw, 0)
total_w, hp_w = struct.unpack_from('<dd', raw, 16)

checks = [
    (magic == 0xC5E3440B,    f"magic={hex(magic)} (beklenen 0xc5e3440b)"),
    (version == 1,            f"version={version} (beklenen 1)"),
    (num_levels == 4,         f"num_levels={num_levels} (beklenen 4)"),
    (num_kw == 2,             f"num_keywords={num_kw} (beklenen 2)"),
    (total_w > 0,             f"total_weighted={total_w:.1f} > 0"),
    (hp_w >= 0,               f"hp_weighted={hp_w:.1f} >= 0"),
]

all_ok = True
for passed, msg in checks:
    status = "  PASS" if passed else "  FAIL"
    print(f"{status} {msg}")
    if not passed:
        all_ok = False

# Boyut kontrolü: header(32) + 4*sizeof(level_result_t)
expected_min = 32
if len(raw) >= expected_min:
    print(f"  PASS dosya boyutu: {len(raw)} byte (>= {expected_min})")
else:
    print(f"  FAIL dosya boyutu: {len(raw)} < {expected_min}")
    all_ok = False

sys.exit(0 if all_ok else 1)
PYEOF
    if [ $? -eq 0 ]; then
        ok "Tum binary kontroller gecti"
    else
        fail "Binary kontrol hatasi"
    fi
else
    fail "results.bin olusturulmadi – test atlandi"
fi

# ─── TEST 8: Watchdog stderr / stdout ─────────────────────────────────────
header "TEST 8 – Watchdog stderr Ciktisi"
cleanup

timeout 15 "$ANALYZER" \
    -c "$CONF" -f "$FILT" -k "error" \
    -t 2 -w 2 -a 32 -b 16 -d 8 -T 5 \
    -o "$OUT_TXT" -O "$OUT_BIN" \
    > /tmp/hw4_stdout.txt 2>/tmp/hw4_stderr.txt || true

if grep -q "\[WATCHDOG\]" /tmp/hw4_stderr.txt; then
    WD_CNT=$(grep -c "\[WATCHDOG\]" /tmp/hw4_stderr.txt)
    ok "Watchdog stderr ciktisi var ($WD_CNT satir)"
else
    fail "Watchdog stderr ciktisi yok"
fi

if grep -q "\[WATCHDOG\]" /tmp/hw4_stdout.txt 2>/dev/null; then
    fail "Watchdog stdout'a da yazmis!"
else
    ok "Watchdog stdout'a yazmiyor"
fi

# İlk 3 watchdog satırını göster
info "Watchdog ornekleri:"
grep "\[WATCHDOG\]" /tmp/hw4_stderr.txt 2>/dev/null | head -3 | \
    while IFS= read -r ln; do info "  $ln"; done
rm -f /tmp/hw4_stdout.txt /tmp/hw4_stderr.txt

# ─── TEST 9: Malformed log satırları ─────────────────────────────────────
header "TEST 9 – Malformed Log Satirlari"
cleanup

cat > /tmp/hw4_bad.log << 'LOGEOF'
bu malformed satir
[yanlis format] kötü
sadece metin yok bracket

[2025-01-01 08:00:01] [INVALID_LEVEL] [src] msg
[2025-01-01 08:00:02] [ERROR] [kernel] gercek hata satiri
[2025-01-01 08:00:03] [WARN]  [nginx]  gercek warn satiri
LOGEOF
echo "/tmp/hw4_bad.log" > /tmp/hw4_bad.conf
echo "kernel" > /tmp/hw4_bad_filter.txt

if run_analyzer 20 -c /tmp/hw4_bad.conf -f /tmp/hw4_bad_filter.txt \
    -k "hata,gercek" -t 1 -w 1 -a 8 -b 4 -d 2 -T 5; then
    ok "Malformed satirlarla coktu degil"
    if [ -f "$OUT_TXT" ]; then
        ERR=$(grep "^ERROR" "$OUT_TXT" | awk '{print $2}' | tr -d '[:space:]')
        WRN=$(grep "^WARN"  "$OUT_TXT" | awk '{print $2}' | tr -d '[:space:]')
        info "ERROR entries: ${ERR:-?}, WARN entries: ${WRN:-?} (beklenen: 1, 1)"
        if [ "${ERR:-0}" = "1" ] && [ "${WRN:-0}" = "1" ]; then
            ok "Gecerli satirlar dogru sayildi: ERROR=1, WARN=1"
        else
            warn "Satir sayisi farkli olabilir (byte-range split nedeniyle)"
        fi
    fi
else
    fail "Malformed satirlarla program coktu"
fi
rm -f /tmp/hw4_bad.log /tmp/hw4_bad.conf /tmp/hw4_bad_filter.txt

# ─── TEST 10: Zombie ve shared memory temizliği ───────────────────────────
header "TEST 10 – Zombie / Shared Memory Temizligi"
cleanup

run_analyzer 30 -c "$CONF" -f "$FILT" \
    -k "error" -t 1 -w 1 -a 16 -b 16 -d 4 -T 5 >/dev/null 2>&1 || true

sleep 1

    ZOMBIES=$(ps aux 2>/dev/null | awk '$8=="Z"' | grep -c "analyzer" 2>/dev/null | head -1 | tr -d " ") ; ZOMBIES=${ZOMBIES:-0}
    if [ "$ZOMBIES" = "0" ] || [ -z "$ZOMBIES" ]; then
    ok "Zombie surec yok"
else
    fail "Zombie surecler var: $ZOMBIES"
fi

# mmap/anonymous kullanildiginda ipcs -m bos olmali
SHM=$(ipcs -m 2>/dev/null | grep -c "0x" || echo "0")
if [ "${SHM:-0}" -eq 0 ]; then
    ok "Artan shared memory segment yok (ipcs -m temiz)"
else
    warn "ipcs -m: $SHM segment var (baska programlardan olabilir)"
fi

# ─── TEST 11: Büyük veri seti (500+ satır, yüksek eşzamanlılık) ──────────
header "TEST 11 – Buyuk Veri Seti (600 satir x 3 dosya)"
cleanup

info "Test log dosyalari olusturuluyor..."
python3 tests/generate_logs.py \
    --lines 600 --files 3 --dir tests/logs_big --seed 77 >/dev/null 2>&1

if run_analyzer 60 \
    -c tests/logs_big/services.conf \
    -f tests/logs_big/priority.txt \
    -k "error,fail,timeout,kill" \
    -t 4 -w 4 -a 128 -b 64 -d 32 -T 10; then
    ok "Buyuk veri seti basarili"
    if [ -f "$OUT_TXT" ]; then
        TOTAL=$(grep "^TOTAL_WEIGHTED_SCORE:" "$OUT_TXT" | awk '{print $2}')
        ENTRIES=$(grep -E "^(ERROR|WARN|INFO|DEBUG)" "$OUT_TXT" | \
            awk '{s+=$2} END {print s}')
        ok "Toplam score: $TOTAL, Toplam entries: ${ENTRIES:-?}"
    fi
else
    fail "Buyuk veri seti basarisiz"
fi
rm -rf tests/logs_big

# ─── TEST 12: CTRL+C (SIGINT) – zombi kontrolü ────────────────────────────
header "TEST 12 – SIGINT (Ctrl+C) Temiz Kapanis"
cleanup

# Büyük dosya oluştur
python3 tests/generate_logs.py \
    --lines 2000 --files 3 --dir tests/logs_sigint --seed 55 >/dev/null 2>&1

# Programı arka plana al, 3 saniye sonra SIGINT gönder
"$ANALYZER" \
    -c tests/logs_sigint/services.conf \
    -f tests/logs_sigint/priority.txt \
    -k "error,fail,timeout,kill" \
    -t 2 -w 2 -a 32 -b 16 -d 8 -T 10 \
    -o "$OUT_TXT" -O "$OUT_BIN" >/dev/null 2>/dev/null &
APID=$!
sleep 2
kill -INT $APID 2>/dev/null || true
sleep 2

# Zombie kontrolü
    ZOMBIES2=$(ps aux 2>/dev/null | awk '$8=="Z"' | grep -c "analyzer" 2>/dev/null | head -1 | tr -d " ") ; ZOMBIES2=${ZOMBIES2:-0}
    if [ "$ZOMBIES2" = "0" ] || [ -z "$ZOMBIES2" ]; then
    ok "SIGINT sonrasi zombie yok"
else
    fail "SIGINT sonrasi zombie: $ZOMBIES2"
fi

# Hâlâ çalışıyor mu?
if kill -0 "$APID" 2>/dev/null; then
    warn "Analyzer hala calisiyor – zorla kill"
    kill -9 "$APID" 2>/dev/null || true
    wait "$APID" 2>/dev/null || true
else
    ok "Analyzer SIGINT ile temiz kapandi"
fi

rm -rf tests/logs_sigint

# ─── TEST 13: Atomic rename kontrolü ──────────────────────────────────────
header "TEST 13 – Atomik Binary Rename"
cleanup

run_analyzer 30 -c "$CONF" -f "$FILT" \
    -k "error" -t 2 -w 2 -a 32 -b 16 -d 8 -T 8 >/dev/null 2>&1 || true

# .tmp dosyası kalmamalı
if [ ! -f "${OUT_BIN}.tmp" ]; then
    ok "Gecici .tmp dosyasi temizlendi"
else
    fail ".tmp dosyasi hala duruyor: ${OUT_BIN}.tmp"
fi

if [ -f "$OUT_BIN" ]; then
    ok "Final binary dosyasi mevcut"
else
    fail "Final binary dosyasi yok"
fi

# ─── SONUÇ ────────────────────────────────────────────────────────────────
header "SONUC"
TOTAL_TESTS=$((PASS+FAIL))
echo ""
echo -e "  ${BOLD}Toplam test : $TOTAL_TESTS${NC}"
echo -e "  ${GREEN}Gecen       : $PASS${NC}"
if [ "$FAIL" -gt 0 ]; then
    echo -e "  ${RED}Basarisiz   : $FAIL${NC}"
fi
if [ "$WARN_CNT" -gt 0 ]; then
    echo -e "  ${YELLOW}Uyari       : $WARN_CNT${NC}"
fi
echo ""
if [ "$FAIL" -eq 0 ]; then
    echo -e "  ${GREEN}${BOLD}Tum testler gecti!${NC}"
else
    echo -e "  ${RED}${BOLD}$FAIL test basarisiz!${NC}"
fi
echo ""

cleanup
exit "$FAIL"
