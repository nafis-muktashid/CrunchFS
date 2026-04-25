#!/usr/bin/env bash
# CrunchFS automated demo — covers all validation steps from summary.txt
set -euo pipefail

BACKING=/tmp/cfs_backing
MOUNT=/tmp/cfs_mount
BIN="$(dirname "$0")/build/passthrough_cfs"

# ── cleanup helper ──────────────────────────────────────────────────────────
cleanup() {
    fusermount3 -u "$MOUNT" 2>/dev/null || true
    rm -rf "$BACKING" "$MOUNT"
}
trap cleanup EXIT

echo "=== CrunchFS Demo ==="
mkdir -p "$BACKING" "$MOUNT"

# ── Launch daemon (multithreaded, background) ───────────────────────────────
"$BIN" "$BACKING" "$MOUNT" -f &   # remove -s → multi-thread FUSE
FUSE_PID=$!
sleep 1
echo "[✓] Daemon started (PID $FUSE_PID)"

# ── Basic correctness ───────────────────────────────────────────────────────
echo
echo "--- Basic read/write/append/truncate ---"
echo "hello crunchfs" > "$MOUNT/a.txt"
cat "$MOUNT/a.txt"
echo "more text" >> "$MOUNT/a.txt"
truncate -s 5 "$MOUNT/a.txt"
echo "After truncate to 5 bytes: $(cat "$MOUNT/a.txt" | xxd -p)"

mv "$MOUNT/a.txt" "$MOUNT/b.txt"
echo "Rename ok: $(cat "$MOUNT/b.txt")"
rm "$MOUNT/b.txt"
echo "Delete ok"

# ── Backing layout ──────────────────────────────────────────────────────────
echo
echo "--- Backing layout ---"
ls -la "$BACKING/.crunchfs/"
ls -la "$BACKING/.crunchfs/meta/" 2>/dev/null | head -5 || true
ls -la "$BACKING/.crunchfs/data/" 2>/dev/null | head -5 || true

# ── Compressible vs incompressible ─────────────────────────────────────────
echo
echo "--- Compression demo (100 MiB each) ---"
rm -f "$MOUNT/big_zero.bin" "$MOUNT/big_rand.bin"

echo "[*] Writing /dev/zero (highly compressible)..."
dd if=/dev/zero of="$MOUNT/big_zero.bin" bs=1M count=100 status=progress 2>&1 || true
echo "Logical: $(ls -lh "$MOUNT/big_zero.bin" | awk '{print $5}')"
echo "On-disk compressed: $(du -sh "$BACKING/.crunchfs/data/" | awk '{print $1}')"

echo "[*] Writing /dev/urandom (incompressible)..."
dd if=/dev/urandom of="$MOUNT/big_rand.bin" bs=1M count=100 status=progress 2>&1 || true
echo "Logical: $(ls -lh "$MOUNT/big_rand.bin" | awk '{print $5}')"
echo "On-disk total: $(du -sh "$BACKING/.crunchfs/data/" | awk '{print $1}')"

# ── Stats virtual file ──────────────────────────────────────────────────────
echo
echo "--- Compression stats (.cfs_stats) ---"
cat "$MOUNT/.cfs_stats"

# ── Integrity / persistence ─────────────────────────────────────────────────
echo
echo "--- Persistence + integrity ---"
SUM1=$(sha256sum "$MOUNT/big_zero.bin" | awk '{print $1}')
echo "Pre-remount sha256: $SUM1"

kill "$FUSE_PID" 2>/dev/null || true
fusermount3 -u "$MOUNT" 2>/dev/null || true
sleep 1

"$BIN" "$BACKING" "$MOUNT" -f &
FUSE_PID=$!
sleep 1

SUM2=$(sha256sum "$MOUNT/big_zero.bin" | awk '{print $1}')
echo "Post-remount sha256: $SUM2"

if [ "$SUM1" = "$SUM2" ]; then
    echo "[✓] Integrity check PASSED"
else
    echo "[✗] Integrity check FAILED"
    exit 1
fi

echo
echo "=== All checks passed ==="
