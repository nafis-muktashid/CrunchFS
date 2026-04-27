#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BIN="${BUILD_DIR}/passthrough_cfs"
BACKING="/tmp/cfs2_demo_backing"
MOUNT="/tmp/cfs2_demo_mount"
LOG="/tmp/cfs2_demo_daemon.log"
PID=""

section() { printf "\n== %s ==\n" "$1"; }
pass() { printf "PASS: %s\n" "$1"; }
fail() {
  printf "FAIL: %s\n" "$1"
  if [[ -f "${LOG}" ]]; then
    printf "\n-- daemon log (tail) --\n"
    tail -n 40 "${LOG}" || true
  fi
  exit 1
}

cleanup() {
  set +e
  if mountpoint -q "${MOUNT}"; then
    fusermount3 -u "${MOUNT}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${PID}" ]] && kill -0 "${PID}" >/dev/null 2>&1; then
    kill "${PID}" >/dev/null 2>&1 || true
    wait "${PID}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

wait_mount() {
  for _ in $(seq 1 80); do
    if mountpoint -q "${MOUNT}"; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_writable() {
  local probe="${MOUNT}/.probe_ready"
  for _ in $(seq 1 80); do
    if echo "ready" > "${probe}" 2>/dev/null; then
      rm -f "${probe}" >/dev/null 2>&1 || true
      return 0
    fi
    sleep 0.1
  done
  return 1
}

section "Build"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" >/dev/null
cmake --build "${BUILD_DIR}" >/dev/null
[[ -x "${BIN}" ]] || fail "binary not found: ${BIN}"
pass "build completed"

section "Prepare"
rm -rf "${BACKING}" "${MOUNT}"
mkdir -p "${BACKING}" "${MOUNT}"
pass "fresh backing and mount prepared"

section "Start daemon"
"${BIN}" "${BACKING}" "${MOUNT}" -f >"${LOG}" 2>&1 &
PID=$!
kill -0 "${PID}" >/dev/null 2>&1 || fail "daemon exited immediately"
wait_mount || fail "mount was not ready"
wait_writable || fail "mount is not writable"
pass "daemon mounted and writable"

section "Basic operations"
echo "hello crunchfs" > "${MOUNT}/a.txt"
echo "more data" >> "${MOUNT}/a.txt"
grep -q "hello crunchfs" "${MOUNT}/a.txt" || fail "read after write failed"
truncate -s 5 "${MOUNT}/a.txt"
[[ "$(cat "${MOUNT}/a.txt")" == "hello" ]] || fail "truncate failed"
mv "${MOUNT}/a.txt" "${MOUNT}/b.txt"
[[ -f "${MOUNT}/b.txt" ]] || fail "rename failed"
rm "${MOUNT}/b.txt"
[[ ! -e "${MOUNT}/b.txt" ]] || fail "unlink failed"
pass "CRUD and rename/truncate verified"

section "Compression behavior"
dd if=/dev/zero of="${MOUNT}/zero.bin" bs=1M count=64 status=none
dd if=/dev/urandom of="${MOUNT}/rand.bin" bs=1M count=32 status=none
logical_zero="$(stat -c '%s' "${MOUNT}/zero.bin")"
disk_total="$(du -sb "${BACKING}/.crunchfs/data" | awk '{print $1}')"
[[ "${logical_zero}" -gt 0 && "${disk_total}" -gt 0 ]] || fail "compression files not created"
pass "compressed chunk storage populated"

section "Stats endpoint"
stats="$(cat "${MOUNT}/.cfs_stats")"
echo "${stats}" | rg -q '^logical_bytes ' || fail "stats missing logical_bytes"
echo "${stats}" | rg -q '^compressed_bytes ' || fail "stats missing compressed_bytes"
echo "${stats}" | rg -q '^ratio ' || fail "stats missing ratio"
pass ".cfs_stats exposed"

section "Concurrent smoke test"
dd if=/dev/zero of="${MOUNT}/w1.bin" bs=1M count=80 status=none &
p1=$!
dd if=/dev/zero of="${MOUNT}/w2.bin" bs=1M count=80 status=none &
p2=$!
wait "${p1}" "${p2}" || fail "parallel writes failed"
sha256sum "${MOUNT}/w1.bin" "${MOUNT}/w2.bin" >/dev/null || fail "parallel read/hash failed"
pass "parallel workload executed"

section "Persistence check"
before="$(sha256sum "${MOUNT}/zero.bin" | awk '{print $1}')"
fusermount3 -u "${MOUNT}"
wait "${PID}" >/dev/null 2>&1 || true
PID=""
"${BIN}" "${BACKING}" "${MOUNT}" -f >"${LOG}" 2>&1 &
PID=$!
wait_mount || fail "remount failed"
wait_writable || fail "remount not writable"
after="$(sha256sum "${MOUNT}/zero.bin" | awk '{print $1}')"
[[ "${before}" == "${after}" ]] || fail "hash mismatch after remount"
pass "persistence and integrity verified"

section "Done"
echo "All checks passed."
