# CrunchFS Report Notes (Phase 3 Complete)

This file is a report-writing companion for the current state of `CrunchFS`.
It summarizes what is completed, what was validated, what to include in the report, and what remains.

---

## 1) Current project status

### Completed

- Phase 1 foundation (FUSE daemon setup, build, mount flow, core filesystem callbacks).
- Phase 2 compressed filesystem flow and correctness.
- Phase 3 modular architecture + thread pool usage + stats endpoint.
- Logical file interface from mount path with compressed storage in backing path.
- On-disk storage layout with:
  - `.crunchfs/meta/*.meta`
  - `.crunchfs/data/*.dat`
  - `.crunchfs/index.txt` for logical path listing support.
- Correctness for basic operations:
  - create, read, write, append, truncate
  - rename, unlink
  - readdir integration for logical files
  - getattr logical size + timestamp behavior
- Reproducible compression demo with compressible vs incompressible input.
- Persistence/integrity check via `sha256sum` across unmount/remount.
- `.cfs_stats` virtual file endpoint implemented and validated.
- One-command full validation script (`demo.sh`) implemented.

### Not completed yet (from original plan)

- Phase 4 polish/extras only (optional):
  - strict SHA-256 path-id mapping (currently deterministic FNV-1a 64-bit hash),
  - extra callbacks/utilities if rubric requires,
  - optional caching/CUDA acceleration pipeline.

---

## 2) What to claim in the report

If reporting up to today:

- "We completed a working modular compressed FUSE filesystem through Phase 3."
- "The mounted path behaves like normal files to user applications, while data is stored in compressed chunk form in backing storage."
- "Thread-safe modular components were integrated (`fuse_ops`, `metadata`, `chunk_store`, `thread_pool`)."
- "Runtime stats are exposed through `/.cfs_stats`."
- "Compression effectiveness was validated using both highly compressible and incompressible data."
- "Data integrity and persistence were validated by hash checks before/after remount."
- "A single command (`./demo.sh`) reproduces end-to-end checks."

Important wording:

- Avoid claiming perfect scaling. Safer wording:
  - "The system is multithread-capable and thread-safe, with worker-pool based parallel chunk-read processing."
  - "Further throughput tuning is a Phase 4 optimization task."

---

## 3) Suggested report structure

1. Problem statement and goal
2. System design overview
3. On-disk format and data path
4. Implementation details (FUSE callbacks + compression pipeline)
5. Experimental setup and commands
6. Results (logical vs compressed size, integrity checks)
7. Limitations and next steps (Phase 4 polish)
8. Conclusion

---

## 4) Architecture summary (report-ready)

### High-level flow

1. User process performs file operation on mount point.
2. Kernel forwards operation to FUSE daemon.
3. Daemon callback executes:
  - For regular files, daemon maps logical path -> internal id.
  - Reads/writes logical bytes with per-file synchronization.
  - Uses modular stores:
    - `MetaStore` for metadata/index
    - `ChunkStore` for compressed chunk files
    - `ThreadPool` for parallel chunk read tasks
  - Persists compressed chunks into `.crunchfs/data` and metadata into `.crunchfs/meta`.
4. Daemon returns response to kernel; app sees normal file behavior.

### Storage semantics

- User-visible logical files exist in mount namespace.
- Backing directory does not necessarily contain plain user file bodies.
- Compressed content and metadata are maintained internally under `.crunchfs`.

---

## 5) Key commands used for reproducible evaluation

## Build

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

- Expected return/output:
  - `mkdir -p` prints nothing on success.
  - `cmake ..` ends with build-system generation confirmation (e.g., "Configuring done", "Generating done").
  - `cmake --build .` ends with successful target build output.
- Meaning:
  - Project configuration and compilation succeeded; executable is available in `build/`.

## Run daemon (multithread-capable mode)

```bash
mkdir -p /tmp/cfs_backing /tmp/cfs_mount
./passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f
```

- Expected return/output:
  - `mkdir -p` prints nothing on success.
  - `./build/passthrough_cfs ... -f` stays in foreground with no prompt return until unmounted.
- Meaning:
  - Filesystem daemon is running and mounted; keep this terminal open during tests.

## Basic correctness

```bash
echo "hello crunchfs" > /tmp/cfs_mount/a.txt
cat /tmp/cfs_mount/a.txt
echo "more text" >> /tmp/cfs_mount/a.txt
truncate -s 5 /tmp/cfs_mount/a.txt
cat /tmp/cfs_mount/a.txt
mv /tmp/cfs_mount/a.txt /tmp/cfs_mount/b.txt
cat /tmp/cfs_mount/b.txt
rm /tmp/cfs_mount/b.txt
```

- Expected return/output:
  - First `cat` prints file contents including appended text.
  - After `truncate -s 5`, second `cat` prints only first 5 bytes (e.g., `hello`).
  - `mv` and `rm` print nothing on success.
- Meaning:
  - Create/read/append/truncate/rename/delete semantics work correctly through mount path.

## Backing layout check

```bash
ls -la /tmp/cfs_backing/.crunchfs
ls -la /tmp/cfs_backing/.crunchfs/meta
ls -la /tmp/cfs_backing/.crunchfs/data
du -h /tmp/cfs_backing/.crunchfs/data
```

- Expected return/output:
  - `ls -la` lists `.crunchfs`, `meta`, and `data` contents.
  - `du -h` prints physical disk usage of stored compressed chunks.
- Meaning:
  - Internal metadata/chunk structure exists and storage is being materialized in backing path.

## Compressible vs incompressible demo

```bash
rm -f /tmp/cfs_mount/big_zero.bin /tmp/cfs_mount/big_rand.bin

dd if=/dev/zero of=/tmp/cfs_mount/big_zero.bin bs=1M count=100 status=progress
ls -lh /tmp/cfs_mount/big_zero.bin
du -h /tmp/cfs_backing/.crunchfs/data

dd if=/dev/urandom of=/tmp/cfs_mount/big_rand.bin bs=1M count=100 status=progress
ls -lh /tmp/cfs_mount/big_rand.bin
du -h /tmp/cfs_backing/.crunchfs/data
```

- Expected return/output:
  - Each `dd` prints transferred bytes and throughput.
  - `ls -lh` shows both files near 100M logical size.
  - `du -h` increases much less for `/dev/zero` data than `/dev/urandom` data.
- Meaning:
  - Filesystem is transparent logically, while compression effectiveness depends on input entropy.

## Persistence + integrity

```bash
sha256sum /tmp/cfs_mount/big_zero.bin
fusermount3 -u /tmp/cfs_mount
./build/passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f
sha256sum /tmp/cfs_mount/big_zero.bin
```

- Expected return/output:
  - First and second `sha256sum` hashes should be identical.
  - `fusermount3 -u` returns silently on successful unmount.
  - Remount command again stays in foreground.
- Meaning:
  - Data persisted in backing store and remained unchanged across unmount/remount.

## Stats endpoint check

```bash
cat /tmp/cfs_mount/.cfs_stats
```

- Expected return/output:
  - Three lines similar to: `logical_bytes ...`, `compressed_bytes ...`, `ratio ...`.
- Meaning:
  - Runtime aggregate counters are exposed correctly through virtual stats file.

## Concurrent smoke check

```bash
dd if=/dev/zero of=/tmp/cfs_mount/w1.bin bs=1M count=80 status=none &
dd if=/dev/zero of=/tmp/cfs_mount/w2.bin bs=1M count=80 status=none &
wait
sha256sum /tmp/cfs_mount/w1.bin /tmp/cfs_mount/w2.bin
```

- Expected return/output:
  - Background `dd` commands return control immediately due to `&`; `wait` blocks until both finish.
  - `sha256sum` prints two hash lines (one for each file).
- Meaning:
  - Parallel write workload completes successfully and produced valid file outputs.

## One-command demo (`demo.sh`)

```bash
./demo.sh
```

- Expected return/output:
  - Script prints step-by-step checks and ends with success summary.
- Meaning:
  - End-to-end validation pipeline (build, run, correctness, compression, stats, persistence, concurrency) passed.

## Cleanup (after demo/testing)

```bash
fusermount3 -u /tmp/cfs_mount 2>/dev/null || true
rm -rf /tmp/cfs_mount /tmp/cfs_backing
rm -rf build
```

- Expected return/output:
  - `fusermount3 -u ... || true` may print nothing (already unmounted case is tolerated).
  - `rm -rf` commands print nothing on success.
- Meaning:
  - Mount is unmounted and temporary runtime/build artifacts are removed for a clean rerun.

What `demo.sh` automatically demonstrates:

- Build + daemon startup readiness.
- Basic CRUD (`create/read/write/truncate/rename/unlink`).
- Backing layout under `.crunchfs`.
- Compression behavior.
- `.cfs_stats` output.
- Concurrent workload smoke.
- Persistence hash check across remount.

---

## 6) How to explain compression behavior in report

- File from `/dev/zero` contains repeated bytes and is highly compressible.
- File from `/dev/urandom` has high entropy and is weakly compressible.
- Therefore:
  - logical size (`ls -lh /tmp/cfs_mount/file`) can be same in both cases
  - physical size (`du -h /tmp/cfs_backing/.crunchfs/data`) differs significantly
- This directly demonstrates transparent compression.

---

## 7) Report-ready code excerpts

Use these snippets from `src/fuse_ops.cpp` to explain core behavior.

### A) FUSE callback registration (daemon contract)

```cpp
static fuse_operations cfs_ops{};
cfs_ops.getattr  = cfs_getattr;
cfs_ops.readdir  = cfs_readdir;
cfs_ops.open     = cfs_open;
cfs_ops.create   = cfs_create;
cfs_ops.read     = cfs_read;
cfs_ops.write    = cfs_write;
cfs_ops.truncate = cfs_truncate;
cfs_ops.unlink   = cfs_unlink;
cfs_ops.rename   = cfs_rename;
cfs_ops.utimens  = cfs_utimens;
cfs_ops.chmod    = cfs_chmod;
```

### B) Logical file size in `getattr`

```cpp
std::string id = g_meta->path_to_id(p);
if (id.empty()) return -ENOENT;
FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
st->st_mode = S_IFREG|m.mode; st->st_nlink = 1;
st->st_size = (off_t)m.logical_size;
st->st_atime = m.atime; st->st_mtime = m.mtime; st->st_ctime = m.ctime;
return 0;
```

### C) Stats virtual file behavior (`/.cfs_stats`)

```cpp
static constexpr const char* STATS_VPATH = "/.cfs_stats";
if (p == STATS_VPATH) {
    std::string sc = stats_content();
    st->st_mode = S_IFREG|0444; st->st_nlink = 1;
    st->st_size = (off_t)sc.length();
    return 0;
}
```

### D) Parallel chunk-read task submission

```cpp
std::vector<std::vector<char>> cdata(nc);
std::atomic<bool> err{false};
for (size_t i = 0; i < nc; ++i) {
    g_pool->enqueue([&, i]{
        if (err) return;
        std::shared_lock<std::shared_mutex> lk(file_lock(id));
        if (g_store->chunk_disk_size(id, fc + i) == 0) {
            cdata[i].assign(ChunkStore::CHUNK_SIZE, '\0');
            return;
        }
        if (!g_store->read_chunk(id, fc + i, cdata[i])) err = true;
    });
}
g_pool->wait_all();
```

### E) Thread pool initialization (safe worker count)

```cpp
size_t workers = std::max<size_t>(1, std::thread::hardware_concurrency());
g_pool  = new ThreadPool(workers);
```

---

## 8) Limitations to state honestly

- Current code is multithread-capable and thread-safe, but performance tuning is still possible.
- Metadata path-id mapping uses deterministic FNV-1a 64-bit hash; strict SHA-256 can be upgraded if rubric requires.
- Current callback surface focuses on file operations used by the project demo; additional callbacks can be added if needed by evaluator scenarios.
- Ownership-change semantics (`chown`) are not implemented; cross-filesystem `mv` into mount can report ownership preservation errors, so demo flow should prefer `cp` + verify + optional source removal.
- CUDA acceleration path is optional and not yet implemented.

---

## 9) Next steps (Phase 4 polish)

- Optional optimization: finer-grained locking and improved parallel throughput.
- Optional standards update: strict SHA-256 path-id mapping.
- Optional features: cache layer and CUDA backend module.
- Report/slides packaging and demo narration cleanup.

---

## 10) One-paragraph conclusion draft

"We implemented and validated a modular transparent compressed filesystem in user space using FUSE. The system preserves normal logical file semantics at the mount point while storing compressed chunk data and metadata internally in the backing store. The implementation includes modular metadata/chunk/thread-pool components, runtime stats via `/.cfs_stats`, and reproducible end-to-end validation through `demo.sh`. Experimental results with compressible (`/dev/zero`) and incompressible (`/dev/urandom`) inputs confirmed expected compression behavior, and integrity checks across remounts verified persistence correctness."