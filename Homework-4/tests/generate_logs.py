#!/usr/bin/env python3
"""
Test log dosyaları üretir.
Kullanım: python3 generate_logs.py [--lines N] [--files N] [--dir DIR]
"""
import random, argparse, os, datetime, string

LEVELS   = ["ERROR","WARN","INFO","DEBUG"]
SOURCES  = ["kernel","nginx","auth","disk","sshd","systemd","cron","network",
            "docker","postgres","redis","app","api","db","cache"]
PRIORITY = ["kernel","nginx","auth"]   # filter.txt'e yazılacaklar

MSGS = [
    "Out of memory: kill process {pid}",
    "Disk usage at {pct}%% on /dev/sda{n}",
    "Connection accepted from 192.168.{a}.{b}",
    "Token verified for user: {user}",
    "Segfault in process {pid} (core dumped)",
    "Retry attempt {n} of 5 for upstream",
    "Failed to bind socket: address in use",
    "Authentication failure for user {user}",
    "SSL error: certificate expired",
    "Database connection timeout after {ms}ms",
    "error processing request: invalid token",
    "fail to allocate memory block",
    "timeout waiting for lock",
    "kill -9 sent to process {pid}",
    "error error: double error occurred",
    "Service started successfully",
    "Loaded configuration from /etc/app.conf",
    "Backup completed: {n} files processed",
    "Cache hit ratio: {pct}%%",
    "Health check passed",
    "Connection closed by remote host",
    "New connection from {a}.{b}.{c}.{d}",
    "User {user} logged in",
    "File {f} not found",
    "Permission denied: /var/log/app.log",
]

def gen_msg():
    t = random.choice(MSGS)
    return t.format(
        pid=random.randint(1000,9999),
        pct=random.randint(50,99),
        n=random.randint(1,5),
        a=random.randint(1,254), b=random.randint(1,254),
        c=random.randint(1,254), d=random.randint(1,254),
        user=random.choice(["admin","root","ubuntu","app","deploy"]),
        ms=random.randint(100,5000),
        f="/var/log/"+random.choice(["app","sys","kern"])+".log",
    )

def gen_line(dt):
    ts  = dt.strftime("%Y-%m-%d %H:%M:%S")
    lvl = random.choice(LEVELS)
    src = random.choice(SOURCES)
    msg = gen_msg()
    return f"[{ts}] [{lvl}] [{src}] {msg}"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lines", type=int, default=200)
    ap.add_argument("--files", type=int, default=3)
    ap.add_argument("--dir",   default="tests/logs")
    ap.add_argument("--seed",  type=int, default=42)
    args = ap.parse_args()

    random.seed(args.seed)
    os.makedirs(args.dir, exist_ok=True)

    log_files = []
    for fi in range(args.files):
        path = os.path.join(args.dir, f"test{fi+1}.log")
        dt = datetime.datetime(2025, 3, 10, 8, 0, 0)
        with open(path, "w") as f:
            for i in range(args.lines):
                # Kimi satırlar kasıtlı malformed
                if random.random() < 0.02:
                    f.write("this is a malformed line without brackets\n")
                elif random.random() < 0.01:
                    f.write("\n")  # boş satır
                else:
                    f.write(gen_line(dt) + "\n")
                dt += datetime.timedelta(seconds=random.randint(1,5))
        log_files.append(path)
        print(f"  Generated: {path}  ({args.lines} lines)")

    # Config dosyası
    conf = os.path.join(args.dir, "services.conf")
    with open(conf, "w") as f:
        for p in log_files:
            f.write(p + "\n")
    print(f"  Config:    {conf}")

    # Filter dosyası
    filt = os.path.join(args.dir, "priority.txt")
    with open(filt, "w") as f:
        for s in PRIORITY:
            f.write(s + "\n")
    print(f"  Filter:    {filt}")

    # Boş filter (Test 5 için)
    empty_filt = os.path.join(args.dir, "empty_filter.txt")
    open(empty_filt, "w").close()
    print(f"  Empty filter: {empty_filt}")

    print(f"\nKullanim:")
    print(f"  ./analyzer -c {conf} -f {filt} \\")
    print(f"    -k \"error,fail,timeout,kill\" \\")
    print(f"    -t 2 -w 2 -a 32 -b 16 -d 8 -T 10 \\")
    print(f"    -o results.txt -O results.bin")

if __name__ == "__main__":
    main()
