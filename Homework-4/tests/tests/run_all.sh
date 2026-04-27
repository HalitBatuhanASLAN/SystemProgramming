#!/usr/bin/env bash
# =============================================================================
# run_all.sh - CSE 344 HW4 Master Test Runner
# =============================================================================
# Tüm test scriptlerini sırayla çalıştırır, her birinin pass/fail durumunu
# raporlar, sonunda kategori bazlı bir özet sunar.
#
# Kullanım:
#   ./run_all.sh                # Tüm testler
#   ./run_all.sh --quick        # Sadece unit testler (hızlı)
#   ./run_all.sh --unit-only    # Sadece unit/
#   ./run_all.sh --integration  # Sadece integration/
#   ./run_all.sh --stress       # Sadece stress/ (uzun sürer)
#   ./run_all.sh --no-stress    # Stress hariç hepsi
#   ./run_all.sh --no-build     # 'make' adımını atla (önceden build)
#   ./run_all.sh --keep-results # Önceki results/ silinmesin
#   ./run_all.sh -h             # Yardım
# =============================================================================

set -u

TESTS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$TESTS_ROOT/.." && pwd)"
ANALYZER_BIN="$PROJECT_ROOT/analyzer"
RESULTS_DIR="$TESTS_ROOT/results"
FIXTURES_DIR="$TESTS_ROOT/fixtures"

export TESTS_ROOT PROJECT_ROOT ANALYZER_BIN RESULTS_DIR FIXTURES_DIR

# common.sh'ı source et (renk kodları + bazı yardımcılar)
source "$TESTS_ROOT/helpers/common.sh"

# ── Argüman parsing ──────────────────────────────────────────────────────────
RUN_UNIT=true
RUN_INTEGRATION=true
RUN_STRESS=true
DO_BUILD=true
KEEP_RESULTS=false

show_help() {
    cat <<EOF
${BOLD}CSE 344 HW4 Master Test Runner${NC}

Usage: $0 [OPTIONS]

Options:
  --quick           Sadece unit testler (en hızlı)
  --unit-only       Sadece tests/unit/ scriptlerini çalıştır
  --integration     Sadece tests/integration/ scriptlerini çalıştır
  --stress          Sadece tests/stress/ scriptlerini çalıştır (uzun)
  --no-stress       Unit + Integration (stress atla)
  --no-build        'make' adımını atla
  --keep-results    Önceki results/ klasörünü silme
  -h, --help        Bu yardımı göster

Çıkış kodu:
  0 - tüm testler başarılı
  1 - bir veya daha fazla test başarısız
  2 - environment / build hatası
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --quick|--unit-only)
            RUN_UNIT=true; RUN_INTEGRATION=false; RUN_STRESS=false ;;
        --integration)
            RUN_UNIT=false; RUN_INTEGRATION=true; RUN_STRESS=false ;;
        --stress)
            RUN_UNIT=false; RUN_INTEGRATION=false; RUN_STRESS=true ;;
        --no-stress)
            RUN_UNIT=true; RUN_INTEGRATION=true; RUN_STRESS=false ;;
        --no-build)
            DO_BUILD=false ;;
        --keep-results)
            KEEP_RESULTS=true ;;
        -h|--help)
            show_help; exit 0 ;;
        *)
            echo -e "${RED}Bilinmeyen argüman:${NC} $1"
            show_help; exit 2 ;;
    esac
    shift
done

# ── Banner ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${BLUE}╔═══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${BLUE}║                                                                   ║${NC}"
echo -e "${BOLD}${BLUE}║       CSE 344 HW4 - Multi-Process Concurrent Log Analysis         ║${NC}"
echo -e "${BOLD}${BLUE}║                  Comprehensive Test Suite                         ║${NC}"
echo -e "${BOLD}${BLUE}║                                                                   ║${NC}"
echo -e "${BOLD}${BLUE}╚═══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "  ${DIM}Project:${NC}    $PROJECT_ROOT"
echo -e "  ${DIM}Tests:${NC}      $TESTS_ROOT"
echo -e "  ${DIM}Binary:${NC}     $ANALYZER_BIN"
echo -e "  ${DIM}Date:${NC}       $(date '+%Y-%m-%d %H:%M:%S')"
echo -e "  ${DIM}Categories:${NC} unit=$RUN_UNIT integration=$RUN_INTEGRATION stress=$RUN_STRESS"
echo ""

# ── Step 1: Build ────────────────────────────────────────────────────────────
if [ "$DO_BUILD" = "true" ]; then
    log_section "1. BUILD"
    cd "$PROJECT_ROOT"

    log_info "Running 'make clean'..."
    make clean > /dev/null 2>&1 || true

    log_info "Running 'make'..."
    BUILD_LOG=$(mktemp)
    if make 2>&1 > "$BUILD_LOG"; then
        BUILD_WARNINGS=$(grep -ci "warning:" "$BUILD_LOG" || echo 0)
        if [ "$BUILD_WARNINGS" -eq 0 ]; then
            log_pass "Build: clean (0 warnings)"
        else
            log_fail "Build: $BUILD_WARNINGS warnings" "(see $BUILD_LOG)"
            cat "$BUILD_LOG" | grep -i "warning:" | head -5
        fi
    else
        echo -e "${RED}${BOLD}BUILD FAILED!${NC}"
        cat "$BUILD_LOG"
        rm -f "$BUILD_LOG"
        exit 2
    fi
    rm -f "$BUILD_LOG"

    if [ ! -x "$ANALYZER_BIN" ]; then
        echo -e "${RED}${BOLD}HATA:${NC} analyzer binary üretilmedi: $ANALYZER_BIN"
        exit 2
    fi
fi

# ── Step 2: Setup environment ────────────────────────────────────────────────
log_section "2. ENVIRONMENT SETUP"

if [ "$KEEP_RESULTS" = "false" ]; then
    rm -rf "$RESULTS_DIR"
fi
mkdir -p "$RESULTS_DIR"
log_info "results/ initialized: $RESULTS_DIR"

# Fixture'ları üret
log_info "Generating test fixtures..."
FIXTURES_DIR="$FIXTURES_DIR" bash "$TESTS_ROOT/helpers/generate_fixtures.sh" \
    > "$RESULTS_DIR/fixtures.log" 2>&1
FIXTURE_COUNT=$(ls "$FIXTURES_DIR" 2>/dev/null | wc -l)
log_info "Fixtures generated: $FIXTURE_COUNT files"

if [ "$FIXTURE_COUNT" -lt 10 ]; then
    echo -e "${RED}HATA:${NC} fixture üretimi başarısız ($FIXTURE_COUNT files)"
    cat "$RESULTS_DIR/fixtures.log"
    exit 2
fi

# Pre-test cleanup: önceki kalıntı süreçleri öldür
pkill -9 analyzer 2>/dev/null || true
sleep 0.2

# ── Step 3: Run tests ────────────────────────────────────────────────────────

# Her test scripti için: pass/fail, exit code, süre
declare -A SCRIPT_STATUS
declare -A SCRIPT_PASSED
declare -A SCRIPT_FAILED
declare -A SCRIPT_SKIPPED
declare -A SCRIPT_TIME

# Tek bir test scriptini çalıştır, sonucu topla
run_test_script() {
    local script="$1"
    local name=$(basename "$script" .sh)
    local logfile="$RESULTS_DIR/${name}.log"

    echo ""
    echo -e "${BOLD}${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
    echo -e "${BOLD}${CYAN}│ Running: $name${NC}"
    echo -e "${BOLD}${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"

    local start=$(date +%s)
    # Çalıştır - hem ekrana hem de log'a yaz
    bash "$script" 2>&1 | tee "$logfile"
    local exit_code=${PIPESTATUS[0]}
    local end=$(date +%s)
    local elapsed=$((end - start))

    # Log'dan sayıları çek - tr ile newline'ları temizle
    local pass=$(grep -c "✓ PASS" "$logfile" 2>/dev/null | tr -d '[:space:]')
    local fail=$(grep -c "✗ FAIL" "$logfile" 2>/dev/null | tr -d '[:space:]')
    local skip=$(grep -c "⊘ SKIP" "$logfile" 2>/dev/null | tr -d '[:space:]')
    pass=${pass:-0}
    fail=${fail:-0}
    skip=${skip:-0}

    SCRIPT_STATUS["$name"]="$exit_code"
    SCRIPT_PASSED["$name"]="$pass"
    SCRIPT_FAILED["$name"]="$fail"
    SCRIPT_SKIPPED["$name"]="$skip"
    SCRIPT_TIME["$name"]="$elapsed"

    # Aralık
    pkill -9 analyzer 2>/dev/null || true
    sleep 0.2
}

# Unit tests
if [ "$RUN_UNIT" = "true" ]; then
    log_section "3. UNIT TESTS"
    for script in "$TESTS_ROOT"/unit/*.sh; do
        [ -f "$script" ] && run_test_script "$script"
    done
fi

# Integration tests
if [ "$RUN_INTEGRATION" = "true" ]; then
    log_section "4. INTEGRATION TESTS"
    for script in "$TESTS_ROOT"/integration/*.sh; do
        [ -f "$script" ] && run_test_script "$script"
    done
fi

# Stress tests
if [ "$RUN_STRESS" = "true" ]; then
    log_section "5. STRESS TESTS"
    echo -e "${YELLOW}  Note: stress testleri 1-3 dakika sürebilir${NC}"
    for script in "$TESTS_ROOT"/stress/*.sh; do
        [ -f "$script" ] && run_test_script "$script"
    done
fi

# ── Step 4: Final summary ───────────────────────────────────────────────────
log_section "FİNAL ÖZET"

TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
TOTAL_TIME=0
SCRIPTS_OK=0
SCRIPTS_FAIL=0

# Tablo başlığı
printf "${BOLD}  %-32s %8s %8s %8s %8s   %s${NC}\n" \
    "Test Script" "Pass" "Fail" "Skip" "Time" "Status"
printf "  %s\n" "─────────────────────────────────────────────────────────────────────────────"

for name in $(echo "${!SCRIPT_STATUS[@]}" | tr ' ' '\n' | sort); do
    pass=${SCRIPT_PASSED["$name"]}
    fail=${SCRIPT_FAILED["$name"]}
    skip=${SCRIPT_SKIPPED["$name"]}
    time_s=${SCRIPT_TIME["$name"]}
    status_code=${SCRIPT_STATUS["$name"]}

    TOTAL_PASS=$((TOTAL_PASS + pass))
    TOTAL_FAIL=$((TOTAL_FAIL + fail))
    TOTAL_SKIP=$((TOTAL_SKIP + skip))
    TOTAL_TIME=$((TOTAL_TIME + time_s))

    if [ "$fail" -eq 0 ] && [ "$status_code" -eq 0 ]; then
        status_str="${GREEN}OK${NC}"
        SCRIPTS_OK=$((SCRIPTS_OK + 1))
    else
        status_str="${RED}FAIL${NC}"
        SCRIPTS_FAIL=$((SCRIPTS_FAIL + 1))
    fi

    printf "  %-32s ${GREEN}%8d${NC} ${RED}%8d${NC} ${YELLOW}%8d${NC} %7ds   %b\n" \
        "$name" "$pass" "$fail" "$skip" "$time_s" "$status_str"
done

printf "  %s\n" "─────────────────────────────────────────────────────────────────────────────"
printf "${BOLD}  %-32s ${GREEN}%8d${NC} ${RED}%8d${NC} ${YELLOW}%8d${NC} %7ds${NC}\n" \
    "TOPLAM" "$TOTAL_PASS" "$TOTAL_FAIL" "$TOTAL_SKIP" "$TOTAL_TIME"

GRAND_TOTAL=$((TOTAL_PASS + TOTAL_FAIL + TOTAL_SKIP))
echo ""
echo -e "  ${BOLD}Toplam test:${NC}      $GRAND_TOTAL"
echo -e "  ${BOLD}${GREEN}Başarılı:${NC}         $TOTAL_PASS"
echo -e "  ${BOLD}${RED}Başarısız:${NC}        $TOTAL_FAIL"
echo -e "  ${BOLD}${YELLOW}Atlandı:${NC}          $TOTAL_SKIP"
echo -e "  ${BOLD}Çalıştırılan script:${NC} $((SCRIPTS_OK + SCRIPTS_FAIL))   ${GREEN}OK=$SCRIPTS_OK${NC}  ${RED}FAIL=$SCRIPTS_FAIL${NC}"
echo -e "  ${BOLD}Toplam süre:${NC}      ${TOTAL_TIME}s"
echo ""

# Final cleanup
pkill -9 analyzer 2>/dev/null || true

if [ "$TOTAL_FAIL" -eq 0 ] && [ "$SCRIPTS_FAIL" -eq 0 ]; then
    echo -e "${BOLD}${GREEN}╔═══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${GREEN}║                  ✓ TÜM TESTLER BAŞARILI                           ║${NC}"
    echo -e "${BOLD}${GREEN}╚═══════════════════════════════════════════════════════════════════╝${NC}"
    exit 0
else
    echo -e "${BOLD}${RED}╔═══════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${BOLD}${RED}║                  ✗ BAZI TESTLER BAŞARISIZ                         ║${NC}"
    echo -e "${BOLD}${RED}╚═══════════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "  Detaylar için: $RESULTS_DIR/<test_name>.log"
    exit 1
fi
