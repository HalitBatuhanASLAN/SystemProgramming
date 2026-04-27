#!/usr/bin/env bash
# =============================================================================
# generate_fixtures.sh - Test verilerini (log dosyaları) deterministik üretir
# =============================================================================
# Tüm fixture'lar deterministik (sabit seed) olduğu için testler tekrarlanabilir.
# =============================================================================

set -e

# Bu script'in bulunduğu dizinden köke ulaş
TESTS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURES_DIR="$TESTS_ROOT/fixtures"

mkdir -p "$FIXTURES_DIR"

# ── Fixture 1: Basit, küçük log (manuel kontrolü kolay) ──────────────────────
cat > "$FIXTURES_DIR/simple_kernel.log" <<'EOF'
[2025-03-10 08:15:02] [ERROR] [kernel] Out of memory: kill process 4821
[2025-03-10 08:15:03] [WARN] [disk] Disk usage at 91% on /dev/sda1
[2025-03-10 08:15:04] [INFO] [nginx] Connection accepted from 192.168.1.5
[2025-03-10 08:15:05] [DEBUG] [auth] Token verified for user: admin
[2025-03-10 08:15:06] [ERROR] [kernel] Segfault in process 4822 (core dumped)
[2025-03-10 08:15:07] [WARN] [nginx] Retry attempt 3 of 5 for upstream
[2025-03-10 08:15:08] [ERROR] [kernel] error errorer multiple error here
EOF

cat > "$FIXTURES_DIR/simple_nginx.log" <<'EOF'
[2025-03-10 09:00:01] [INFO] [nginx] Server started on port 80
[2025-03-10 09:00:02] [WARN] [nginx] Slow upstream response timeout
[2025-03-10 09:00:03] [ERROR] [nginx] Backend connection failed
[2025-03-10 09:00:04] [DEBUG] [nginx] Health check passed
[2025-03-10 09:00:05] [DEBUG] [nginx] Test malformed line below

[2025-03-10 09:00:06] [BADLEVEL] [foo] should be skipped
[2025-03-10 09:00:07] [INFO] [nginx] Last entry
EOF

cat > "$FIXTURES_DIR/simple.conf" <<'EOF'
simple_kernel.log
simple_nginx.log
EOF

cat > "$FIXTURES_DIR/simple_priority.txt" <<'EOF'
kernel
auth
EOF

cat > "$FIXTURES_DIR/empty_priority.txt" <<'EOF'
EOF

# ── Fixture 2: Overlapping keyword test ──────────────────────────────────────
# 'errorerorerror' içinde 'error' = 2 (sliding window), 'aaaaa' içinde 'aa' = 4
cat > "$FIXTURES_DIR/overlap.log" <<'EOF'
[2025-03-10 08:15:08] [ERROR] [kernel] errorerorerror
[2025-03-10 08:15:09] [ERROR] [kernel] aaaaa
[2025-03-10 08:15:10] [ERROR] [kernel] no match here
EOF

cat > "$FIXTURES_DIR/overlap.conf" <<'EOF'
overlap.log
EOF

cat > "$FIXTURES_DIR/overlap_priority.txt" <<'EOF'
kernel
EOF

# ── Fixture 3: Tek satırlık log (corner case) ────────────────────────────────
cat > "$FIXTURES_DIR/single_line.log" <<'EOF'
[2025-03-10 08:15:00] [ERROR] [test] just one error line
EOF

cat > "$FIXTURES_DIR/single.conf" <<'EOF'
single_line.log
EOF

# ── Fixture 4: Tüm satırlar malformed (parser robustness) ────────────────────
cat > "$FIXTURES_DIR/all_malformed.log" <<'EOF'
no brackets at all
[BADTIME] [ERROR] [src] missing date format
random text without any structure
[2025-03-10 08:15:00] missing level
[2025-03-10 08:15:01] [BAD] [src] bad level
EOF

cat > "$FIXTURES_DIR/malformed.conf" <<'EOF'
all_malformed.log
EOF

# ── Fixture 5: Boş dosya ─────────────────────────────────────────────────────
: > "$FIXTURES_DIR/empty_file.log"

cat > "$FIXTURES_DIR/empty.conf" <<'EOF'
empty_file.log
EOF

# ── Fixture 6: Tek seviyeli (sadece ERROR) - score deterministik test ────────
cat > "$FIXTURES_DIR/error_only.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [a] error
[2025-03-10 08:00:01] [ERROR] [a] fail
[2025-03-10 08:00:02] [ERROR] [a] timeout
[2025-03-10 08:00:03] [ERROR] [b] error fail timeout
[2025-03-10 08:00:04] [ERROR] [b] no match
EOF

cat > "$FIXTURES_DIR/error_only.conf" <<'EOF'
error_only.log
EOF

cat > "$FIXTURES_DIR/error_priority.txt" <<'EOF'
a
EOF

# ── Fixture 7: Büyük log (yük testi - 1000 satır) ────────────────────────────
python3 << 'PYEOF'
import random, os
random.seed(42)
levels = ['ERROR','WARN','INFO','DEBUG']
sources = ['kernel','nginx','auth','disk','app','db','sys','net']
words = ['error','fail','timeout','success','OK','retry','done',
        'process','connection','memory','data','request','response','status']
fixtures_dir = os.environ.get('FIXTURES_DIR', '.')
for fname, n in [('big1.log', 400), ('big2.log', 400), ('big3.log', 200)]:
    path = os.path.join(fixtures_dir, fname)
    with open(path,'w') as f:
        for i in range(n):
            lvl = random.choice(levels)
            src = random.choice(sources)
            n_words = random.randint(3,8)
            msg = ' '.join(random.choice(words) for _ in range(n_words))
            f.write(f'[2025-03-10 08:{i//60:02d}:{i%60:02d}] [{lvl}] [{src}] {msg}\n')
PYEOF

cat > "$FIXTURES_DIR/big.conf" <<'EOF'
big1.log
big2.log
big3.log
EOF

cat > "$FIXTURES_DIR/big_priority.txt" <<'EOF'
kernel
auth
sys
EOF

# ── Fixture 8: Çok büyük log (stress testi - 5000 satır) ─────────────────────
python3 << 'PYEOF'
import random, os
random.seed(123)
levels = ['ERROR','WARN','INFO','DEBUG']
sources = ['kernel','nginx','auth','disk','app','db','sys','net','svc','daemon']
words = ['error','fail','timeout','OK','retry']
fixtures_dir = os.environ.get('FIXTURES_DIR', '.')
for fname, n in [('huge1.log', 2500), ('huge2.log', 2500)]:
    path = os.path.join(fixtures_dir, fname)
    with open(path,'w') as f:
        for i in range(n):
            lvl = random.choice(levels)
            src = random.choice(sources)
            n_words = random.randint(3,12)
            msg = ' '.join(random.choice(words) for _ in range(n_words))
            f.write(f'[2025-03-10 {i//3600:02d}:{(i%3600)//60:02d}:{i%60:02d}] [{lvl}] [{src}] {msg}\n')
PYEOF

cat > "$FIXTURES_DIR/huge.conf" <<'EOF'
huge1.log
huge2.log
EOF

cat > "$FIXTURES_DIR/huge_priority.txt" <<'EOF'
kernel
EOF

# ── Fixture 9: Determinism testi - bilinen kesin skor ────────────────────────
# 4 satır, her biri tam olarak 1 'error' + 1 'fail' içeriyor
# ERROR weight=4, WARN=2, INFO=1, DEBUG=0.5
# Beklenen toplam: 4*(4+4) + 2*(4+4) + 1*(4+4) + 0.5*(4+4) = 32+16+8+4 = 60
cat > "$FIXTURES_DIR/known_score.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [src1] error fail
[2025-03-10 08:00:01] [WARN] [src1] error fail
[2025-03-10 08:00:02] [INFO] [src1] error fail
[2025-03-10 08:00:03] [DEBUG] [src1] error fail
EOF

cat > "$FIXTURES_DIR/known_score.conf" <<'EOF'
known_score.log
EOF

cat > "$FIXTURES_DIR/known_score_priority.txt" <<'EOF'
src1
EOF

# ── Fixture 10: Long line testi ──────────────────────────────────────────────
python3 << 'PYEOF'
import os
fixtures_dir = os.environ.get('FIXTURES_DIR', '.')
path = os.path.join(fixtures_dir, 'long_lines.log')
with open(path,'w') as f:
    # Her satır ~200 karakter; 'error' kelimesi 5 kez geçiyor
    msg_base = 'error fail timeout '
    for i in range(50):
        msg = msg_base * 10  # 5 'error' per line
        f.write(f'[2025-03-10 08:{i//60:02d}:{i%60:02d}] [ERROR] [longsrc] {msg}\n')
PYEOF

cat > "$FIXTURES_DIR/long_lines.conf" <<'EOF'
long_lines.log
EOF

# ── Fixture 11: Multi-source diversity ──────────────────────────────────────
cat > "$FIXTURES_DIR/multi_source.log" <<'EOF'
[2025-03-10 08:00:00] [ERROR] [alpha] msg1
[2025-03-10 08:00:01] [ERROR] [beta] msg2
[2025-03-10 08:00:02] [ERROR] [gamma] msg3
[2025-03-10 08:00:03] [ERROR] [delta] msg4
[2025-03-10 08:00:04] [ERROR] [alpha] msg5
[2025-03-10 08:00:05] [ERROR] [beta] msg6
[2025-03-10 08:00:06] [ERROR] [gamma] msg7
[2025-03-10 08:00:07] [ERROR] [alpha] msg8
EOF
# alpha:3, beta:2, gamma:2, delta:1 → top-3: alpha, beta, gamma

cat > "$FIXTURES_DIR/multi_source.conf" <<'EOF'
multi_source.log
EOF

cat > "$FIXTURES_DIR/multi_source_priority.txt" <<'EOF'
alpha
EOF

echo "Fixture'lar oluşturuldu: $(ls "$FIXTURES_DIR" | wc -l) dosya"
ls "$FIXTURES_DIR"
