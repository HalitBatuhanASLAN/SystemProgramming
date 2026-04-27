# CSE 344 HW4 - Test Suite

Multi-Process Concurrent Log Analysis Pipeline için kapsamlı test paketi.

## Hızlı Başlangıç

```bash
# Önce projeyi build et (veya run_all.sh otomatik yapar)
make

# Tüm testleri çalıştır
cd tests
./run_all.sh

# Sadece hızlı unit testler
./run_all.sh --quick

# Stres testleri hariç
./run_all.sh --no-stress
```

## Klasör Yapısı

```
tests/
├── run_all.sh                      # Master test runner (BURADAN BAŞLA)
├── helpers/
│   ├── common.sh                   # Renk kodları, assert ve yardımcı fonksiyonlar
│   └── generate_fixtures.sh        # Test verisi (log, conf, priority) üretici
├── fixtures/                       # Otomatik üretilen test verileri
├── unit/                           # Hızlı, izole birim testler (<5s/script)
│   ├── 01_cli_args.sh              # CLI argüman doğrulama (10 test)
│   ├── 02_keyword_counting.sh      # Sliding-window keyword sayımı (6 test)
│   ├── 03_parser_robustness.sh     # Malformed/empty/CRLF (7 test)
│   ├── 04_output_format.sh         # PDF format uyumu (14 test)
│   ├── 05_per_thread.sh            # TLS destructor + per-thread skor (7 test)
│   ├── 06_top_sources.sh           # Top-3 source seçimi (5 test)
│   ├── 07_priority_filter.sh       # HIGH_PRIORITY_SCORE (6 test)
│   └── 08_binary_format.sh         # Magic 0xC5E3440B (8 test)
├── integration/                    # End-to-end + signal + statik analiz
│   ├── 10_full_pipeline.sh         # Tam pipeline çalışması (12 test)
│   ├── 11_determinism.sh           # Tekrarlanabilirlik (8 test)
│   ├── 12_signals.sh               # SIGINT/SIGTERM (6 test)
│   ├── 13_timeout.sh               # -T flag (5 test)
│   └── 14_source_compliance.sh     # PDF mandatory primitive'ler (~25 check)
├── stress/                         # Yüksek concurrency, uzun yük (1-3 dk)
│   ├── 20_high_concurrency.sh      # t=16, w=8, race detection (8 test)
│   ├── 21_memory_stress.sh         # FD/shm leak, RSS izleme (~7 test)
│   └── 22_long_running.sh          # 10x ardışık huge run (~10 test)
└── results/                        # Test çıktıları (otomatik oluşturulur)
    ├── *.log                       # Her script için tam log
    ├── *.txt / *.bin               # analyzer'dan üretilen output dosyaları
    └── fixtures.log                # Fixture üretim logu
```

## CLI Bayrakları

| Bayrak | Açıklama |
|--------|----------|
| `--quick` veya `--unit-only` | Sadece `unit/` (en hızlı, ~30s) |
| `--integration` | Sadece `integration/` |
| `--stress` | Sadece `stress/` (uzun, 2-5 dk) |
| `--no-stress` | Unit + Integration |
| `--no-build` | `make` adımını atla |
| `--keep-results` | `results/` silinmesin |
| `-h`, `--help` | Yardım |

## Çıkış Kodu

- **0** : Tüm testler başarılı
- **1** : Bir veya daha fazla test başarısız
- **2** : Environment / build hatası

## Tek Bir Test Scriptini Çalıştırma

`run_all.sh` aracılığıyla değil, doğrudan:

```bash
# Önce build
make

# Önce fixture'ları üret
bash tests/helpers/generate_fixtures.sh

# Tek bir testi çalıştır
bash tests/unit/02_keyword_counting.sh
```

## Test Çıktı Formatı

Her test şöyle görünür:

```
─── 2.1 Overlapping keyword matches ───
  ✓ PASS  ERROR.error = 8.0 (2 occurrences × weight 4)
  ✓ PASS  ERROR.aa = 16.0 (4 occurrences × weight 4)
  ✗ FAIL  TOTAL_WEIGHTED_SCORE = 24.0
         expected='24.0' got='20.0'
  ⊘ SKIP  watchdog message (may run silently)
```

- **✓ PASS**  : Test geçti
- **✗ FAIL**  : Test başarısız (gerekçesi alt satırda)
- **⊘ SKIP**  : Test ortamda çalıştırılamıyor (zararsız)

## Test Kategorileri (Detay)

### Unit Tests (`unit/`)
Tek bir özelliği yalıtılmış olarak test eder. `analyzer` binary'sinin
küçük fixture'lar üzerinde tek bir invokasyonu üzerinde çalışır.
Her script **bağımsız** çalışabilir (common.sh source eder).

### Integration Tests (`integration/`)
Tüm pipeline'ı (Reader → Dispatcher → Analyzer × 4 → Aggregator + Watchdog)
gerçek senaryolarla çalıştırır. Ayrıca:
- `12_signals.sh` SIGINT/SIGTERM ve zombie/leak kontrolü yapar.
- `13_timeout.sh` `-T` bayrağının davranışını test eder.
- `14_source_compliance.sh` PDF zorunlu API'lerin (pthread_key_t, sem_t, syscall(SYS_gettid), MAP_SHARED, vb.) kullanıldığını **statik olarak** doğrular.

### Stress Tests (`stress/`)
Race condition ve resource leak yakalamak için tasarlanmıştır.
- `20_high_concurrency.sh` : t=1..16, w=1..8 varyantları, hepsi aynı toplam skoru üretmeli.
- `21_memory_stress.sh` : FD/shm sızıntısı, RSS büyümesi.
- `22_long_running.sh` : 10 ardışık huge run, çok dosyalı config.

## Gereksinimler

- Bash 4+ (associative array)
- gcc + make + pthread
- Python 3 (binary header parsing için)
- Linux (`/proc/self/fd`, `/dev/shm`, `pgrep`, `ipcs` opsiyonel)

## Yaygın Sorunlar

**"analyzer binary bulunamadı"**
→ Önce `make` çalıştırın (veya `--no-build` bayrağı vermeyin).

**"BUILD FAILED!"**
→ Compile hatası var. `run_all.sh` build log'unu ekrana basar.

**Stress testleri çok yavaş**
→ Container/VM'de `--no-stress` bayrağıyla çalıştırın; sonuç hâlâ kapsamlıdır.

**SIGINT testleri SKIP olarak görünüyor**
→ Process küçük log'da o kadar hızlı bitiyor ki sinyal gönderemiyoruz.
   Zararsız bir durum, atlanması doğru.

## Mimari Notlar

`run_all.sh` her test scriptini `bash <script>` ile spawn eder ve `tee` ile
hem ekrana hem de `results/<name>.log` dosyasına yazar. Sonucu ✓ PASS / ✗ FAIL
satırlarını sayarak toplar. Bu sayede:

1. Her test scripti **bağımsız** çalışabilir.
2. Bir scriptin çökmesi diğerlerini etkilemez.
3. Kategori bazlı özet çıkartmak kolaydır.
4. Loglar diskte kalır, post-mortem inceleme yapılabilir.

## Özelleştirme

Test parametrelerini değiştirmek için ilgili scripti edit edin. Çoğu test:
- `-t` (parser thread)
- `-w` (worker thread)
- `-a` (Region A buffer)
- `-b` (Region B buffer)
- `-d` (Region D buffer)
- `-T` (timeout)

argümanlarını değiştirerek farklı senaryolar deneyebilir.
