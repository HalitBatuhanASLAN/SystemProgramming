# =============================================================================
# common.sh - Tüm test scriptleri tarafından source edilen ortak fonksiyonlar
# =============================================================================
# Renk kodları, assert fonksiyonları, log/yardımcı fonksiyonlar
# =============================================================================

# ── Renk kodları ─────────────────────────────────────────────────────────────
if [ -t 1 ]; then
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    CYAN='\033[0;36m'
    BOLD='\033[1m'
    DIM='\033[2m'
    NC='\033[0m'
else
    RED=''; GREEN=''; YELLOW=''; BLUE=''; CYAN=''; BOLD=''; DIM=''; NC=''
fi

# ── Global sayaçlar ──────────────────────────────────────────────────────────
TESTS_PASSED=${TESTS_PASSED:-0}
TESTS_FAILED=${TESTS_FAILED:-0}
TESTS_SKIPPED=${TESTS_SKIPPED:-0}
TEST_FAILURES=""

# ── Yapılandırma ─────────────────────────────────────────────────────────────
# Bu değişkenler test scriptlerinin çağrılmasından önce ayarlanmış olmalı.
# Eğer ayarlı değilse default'lar kullanılır.
TESTS_ROOT="${TESTS_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$TESTS_ROOT/.." && pwd)}"
ANALYZER_BIN="${ANALYZER_BIN:-$PROJECT_ROOT/analyzer}"
RESULTS_DIR="${RESULTS_DIR:-$TESTS_ROOT/results}"
FIXTURES_DIR="${FIXTURES_DIR:-$TESTS_ROOT/fixtures}"

# ── Loglama fonksiyonları ────────────────────────────────────────────────────

log_section() {
    echo ""
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${BLUE} $1${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════════${NC}"
}

log_subsection() {
    echo ""
    echo -e "${BOLD}${CYAN}─── $1 ───${NC}"
}

log_info() {
    echo -e "${DIM}  ℹ  $1${NC}"
}

log_pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "  ${GREEN}✓ PASS${NC}  $1"
}

log_fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    TEST_FAILURES="${TEST_FAILURES}\n  - $1"
    echo -e "  ${RED}✗ FAIL${NC}  $1"
    if [ -n "$2" ]; then
        echo -e "${DIM}         ${2}${NC}"
    fi
}

log_skip() {
    TESTS_SKIPPED=$((TESTS_SKIPPED + 1))
    echo -e "  ${YELLOW}⊘ SKIP${NC}  $1${2:+ ($2)}"
}

# ── Assert fonksiyonları ─────────────────────────────────────────────────────
# Her assert: başarılıysa log_pass, başarısızsa log_fail çağırır.

assert_eq() {
    # assert_eq <name> <expected> <actual>
    local name="$1"; local expected="$2"; local actual="$3"
    if [ "$expected" = "$actual" ]; then
        log_pass "$name"
    else
        log_fail "$name" "expected='$expected' got='$actual'"
    fi
}

assert_neq() {
    local name="$1"; local unexpected="$2"; local actual="$3"
    if [ "$unexpected" != "$actual" ]; then
        log_pass "$name"
    else
        log_fail "$name" "got '$actual' which should not equal '$unexpected'"
    fi
}

assert_contains() {
    # assert_contains <name> <haystack> <needle>
    local name="$1"; local haystack="$2"; local needle="$3"
    if echo "$haystack" | grep -qF -- "$needle"; then
        log_pass "$name"
    else
        log_fail "$name" "needle='$needle' not found"
    fi
}

assert_not_contains() {
    local name="$1"; local haystack="$2"; local needle="$3"
    if echo "$haystack" | grep -qF -- "$needle"; then
        log_fail "$name" "found unexpected '$needle'"
    else
        log_pass "$name"
    fi
}

assert_file_exists() {
    local name="$1"; local file="$2"
    if [ -f "$file" ]; then
        log_pass "$name"
    else
        log_fail "$name" "file does not exist: $file"
    fi
}

assert_file_nonempty() {
    local name="$1"; local file="$2"
    if [ -s "$file" ]; then
        log_pass "$name"
    else
        log_fail "$name" "file empty or missing: $file"
    fi
}

assert_exit_code() {
    # assert_exit_code <name> <expected_code> <actual_code>
    local name="$1"; local expected="$2"; local actual="$3"
    if [ "$expected" = "$actual" ]; then
        log_pass "$name"
    else
        log_fail "$name" "expected exit=$expected got=$actual"
    fi
}

assert_grep() {
    # assert_grep <name> <pattern> <file>
    local name="$1"; local pattern="$2"; local file="$3"
    if grep -qE "$pattern" "$file" 2>/dev/null; then
        log_pass "$name"
    else
        log_fail "$name" "pattern '$pattern' not found in $file"
    fi
}

assert_grep_count() {
    # assert_grep_count <name> <pattern> <file> <expected_count>
    local name="$1"; local pattern="$2"; local file="$3"; local expected="$4"
    local actual=$(grep -cE "$pattern" "$file" 2>/dev/null || echo 0)
    if [ "$expected" = "$actual" ]; then
        log_pass "$name"
    else
        log_fail "$name" "expected $expected matches of '$pattern' got $actual"
    fi
}

assert_numeric_eq() {
    # Float karşılaştırması (1 ondalık basamak hassasiyet)
    local name="$1"; local expected="$2"; local actual="$3"
    if awk -v e="$expected" -v a="$actual" 'BEGIN { exit !(e+0 == a+0) }'; then
        log_pass "$name"
    else
        log_fail "$name" "expected=$expected got=$actual"
    fi
}

assert_numeric_in_range() {
    # Tolerans değerli karşılaştırma: |expected - actual| <= tolerance
    local name="$1"; local expected="$2"; local actual="$3"; local tolerance="${4:-0.01}"
    if awk -v e="$expected" -v a="$actual" -v t="$tolerance" \
           'BEGIN { d=e-a; if(d<0)d=-d; exit !(d<=t) }'; then
        log_pass "$name"
    else
        log_fail "$name" "expected≈$expected got=$actual (tol=$tolerance)"
    fi
}

# ── Yardımcı fonksiyonlar ────────────────────────────────────────────────────

# Belirli bir alanı output dosyasından çek (örn. TOTAL_WEIGHTED_SCORE: 19.0)
extract_field() {
    local file="$1"; local field="$2"
    grep "^$field:" "$file" 2>/dev/null | head -1 | awk -F': ' '{print $2}'
}

# Belirli bir LEVEL satırının (örn. ERROR) belirli bir kolonunu çek
# Format:  LEVEL  ENTRIES  WEIGHTED_SCORE  ... keywords ...
extract_level_col() {
    local file="$1"; local level="$2"; local col="$3"
    awk -v lvl="$level" -v c="$col" '
        $1 == lvl { print $c }
    ' "$file" 2>/dev/null | head -1
}

# Per-thread satırından bir thread'in skorunu çek
# Format:  ERROR    thread_0:12.0  thread_1:4.0
extract_thread_score() {
    local file="$1"; local level="$2"; local thread_id="$3"
    awk -v lvl="$level" -v tid="thread_${thread_id}:" '
        /^# Per-thread/ { p=1; next }
        p && $1 == lvl {
            for (i=2; i<=NF; i++) {
                if (index($i, tid) == 1) {
                    split($i, a, ":")
                    print a[2]
                    exit
                }
            }
        }
    ' "$file" 2>/dev/null
}

# Top-3 source satırından kaynağı bul
# Format:  ERROR    kernel:3  nginx:1
extract_top_source_count() {
    local file="$1"; local level="$2"; local source="$3"
    awk -v lvl="$level" -v src="${source}:" '
        /^# Top-3/ { p=1; next }
        p && $1 == lvl {
            for (i=2; i<=NF; i++) {
                if (index($i, src) == 1) {
                    split($i, a, ":")
                    print a[2]
                    exit
                }
            }
        }
    ' "$file" 2>/dev/null
}

# Binary file'ın magic header'ını oku
read_binary_magic() {
    local bin="$1"
    python3 -c "
import struct, sys
try:
    with open('$bin','rb') as f:
        data = f.read(4)
        if len(data) < 4:
            print('SHORT')
            sys.exit(0)
        m = struct.unpack('<I', data)[0]
        print(f'0x{m:08X}')
except Exception as e:
    print(f'ERR:{e}')
"
}

# Binary header'ı detaylı oku
read_binary_header() {
    local bin="$1"
    python3 << EOF
import struct, sys
try:
    with open('$bin','rb') as f:
        data = f.read(32)
    if len(data) < 32:
        print(f'SHORT_HEADER:{len(data)}')
        sys.exit(0)
    magic, version, num_levels, num_keywords, total_w, hp_w = \
        struct.unpack('<IIIIdd', data)
    print(f'magic=0x{magic:08X} version={version} levels={num_levels} '
          f'keywords={num_keywords} total={total_w} hp={hp_w}')
except Exception as e:
    print(f'ERR:{e}')
EOF
}

# Process'in (ve subprocess'lerinin) hâlâ çalışıp çalışmadığını kontrol et
process_alive() {
    local pid="$1"
    kill -0 "$pid" 2>/dev/null
}

# Tüm zombie ve orphan analyzer process'lerini temizle
cleanup_processes() {
    pkill -9 analyzer 2>/dev/null
    sleep 0.2
    # Defunct (zombie) kontrolü
    local zombies=$(ps -eo state,comm | awk '$1=="Z" && $2~/analyzer/' | wc -l)
    if [ "$zombies" -gt 0 ]; then
        log_info "Note: $zombies defunct analyzer processes (will be reaped)"
    fi
}

# IPC kaynaklarını temizle (POSIX shared memory + sem)
cleanup_ipc() {
    # POSIX shared memory + named semaphore'ları kontrol et
    if [ -d /dev/shm ]; then
        # Bu projede yalnızca anonymous mmap kullanılıyor; ama yine de güvenlik için
        find /dev/shm -maxdepth 1 -name 'sem.*' -user "$(whoami)" -delete 2>/dev/null || true
    fi
}

# Bir komutu timeout ile çalıştır, çıktıyı dosyaya yaz, exit kodu döndür
run_with_timeout() {
    local timeout_sec="$1"; shift
    local stdout_file="$1"; shift
    local stderr_file="$1"; shift
    timeout --preserve-status -k 2 "$timeout_sec" "$@" \
        > "$stdout_file" 2> "$stderr_file"
    return $?
}

# Test başlığını yazdır
print_test_header() {
    echo ""
    echo -e "${BOLD}╔═══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}║ $1${NC}"
    echo -e "${BOLD}╚═══════════════════════════════════════════════════════════════════╝${NC}"
}

# Ortak setup: results dir oluştur, binary kontrol et
ensure_environment() {
    mkdir -p "$RESULTS_DIR"
    if [ ! -x "$ANALYZER_BIN" ]; then
        echo -e "${RED}HATA:${NC} analyzer binary bulunamadı: $ANALYZER_BIN"
        echo "       Önce 'make' çalıştırın."
        exit 2
    fi
}

# Final özet (her test scriptinin sonunda çağırılır)
print_summary() {
    local total=$((TESTS_PASSED + TESTS_FAILED + TESTS_SKIPPED))
    echo ""
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD} TEST ÖZETİ${NC}"
    echo -e "${BOLD}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "  Toplam        : ${BOLD}$total${NC}"
    echo -e "  ${GREEN}Başarılı${NC}     : $TESTS_PASSED"
    echo -e "  ${RED}Başarısız${NC}    : $TESTS_FAILED"
    echo -e "  ${YELLOW}Atlandı${NC}      : $TESTS_SKIPPED"

    if [ "$TESTS_FAILED" -gt 0 ]; then
        echo ""
        echo -e "${RED}${BOLD}BAŞARISIZ TESTLER:${NC}"
        echo -e "$TEST_FAILURES"
        echo ""
        return 1
    fi
    return 0
}
