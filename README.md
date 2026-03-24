# CrunchFS

**Multithreaded transparent compressed FUSE-based filesystem** — CSE323 OS Project.

A FUSE filesystem on Fedora (C/C++) that transparently compresses file data in chunks using multithreading, with a clear way to show compression (`du`, stats) and a design that leaves room for future CUDA integration.

---

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Foundation: Fedora + libfuse3, read-only passthrough prototype (`fuse-test/`), path mapping; full CMake layout & metadata design optional next | **Done** (prototype) |
| **Phase 2** | Compression: write/read paths, chunking, backing layout, single-threaded correctness | Not started |
| **Phase 3** | Multithreading, thread pool, stats (`.cfs_stats` or `cfsctl`), error handling | Not started |
| **Phase 4** | Polish: demo script, report/slides, optional caching, CUDA-ready interface | Not started |

**Overall:** Phase 1 passthrough works on Linux (`fuse-test/passthrough_cfs.cpp`). Compression and main `src/` tree are next.

---

## File structure

```
CrunchFS/
├── CMakeLists.txt          # (when wired for main project)
├── README.md
├── summary.txt
├── fuse-test/              # Phase 1 prototype
│   ├── passthrough_cfs.cpp
│   └── hello_fs.cpp        # minimal FUSE demo (optional)
├── src/                    # Planned main tree
│   ├── main.cpp
│   ├── fuse_ops.cpp/hpp
│   ├── metadata.cpp/hpp
│   ├── chunk_store.cpp/hpp
│   ├── compression/
│   │   ├── interface.hpp
│   │   └── zstd_backend.cpp/hpp
│   └── thread_pool.cpp/hpp
├── include/crunchfs/
├── tests/
└── docs/
```

---

## Prerequisites (Fedora)

```bash
sudo dnf install fuse3 fuse3-devel gcc-c++ cmake make
```

- **Compression** (Phase 2+): e.g. `sudo dnf install zstd zstd-devel` or `lz4 lz4-devel`

---

## Phase 1 prototype — build (`fuse-test`)

From the repo root:

```bash
cd fuse-test
g++ -std=c++17 passthrough_cfs.cpp -lfuse3 -o passthrough_cfs
```

`FUSE_USE_VERSION` in source must match your installed libfuse3 (this tree uses 36 for current Fedora libfuse3).

---

## Phase 1 prototype — run

**Terminal A** (foreground; shows errors):

```bash
mkdir -p /tmp/cfs_backing /tmp/cfs_mount
echo "hello" > /tmp/cfs_backing/a.txt
cd fuse-test
./passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f
```

**Terminal B**:

```bash
ls -la /tmp/cfs_mount
cat /tmp/cfs_mount/a.txt
```

Optional subdirectory check (create under backing, then list through mount):

```bash
mkdir -p /tmp/cfs_backing/a/b
echo "nested" > /tmp/cfs_backing/a/b/c.txt
ls -la /tmp/cfs_mount/a/b
cat /tmp/cfs_mount/a/b/c.txt
```

---

## Unmount

- If `passthrough_cfs` is in the foreground: **Ctrl+C** in that terminal, or  
- From another shell:

```bash
fusermount3 -u /tmp/cfs_mount
```

(On older setups, `fusermount -u /tmp/cfs_mount` instead.)

Verify: `mount | grep cfs_mount` should show nothing.

---

## Verification (target — when compression exists)

- **Logical size:** `ls -lh <mountpoint>/file`
- **Compressed size:** `du -h <backing_store_path>`
- **Stats:** `/mnt/cfs/.cfs_stats` or `cfsctl stats` (planned)

---

## Project plan

See **[summary.txt](summary.txt)** for the full phased plan, constraints, and CUDA-ready design notes.

---

## Team

3 people. Target: early April (e.g. 1 April).
