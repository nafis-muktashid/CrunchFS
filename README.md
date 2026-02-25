# CrunchFS

**Multithreaded transparent compressed FUSE-based filesystem** — CSE323 OS Project.

A FUSE filesystem on Fedora (C/C++) that transparently compresses file data in chunks using multithreading, with a clear way to show compression (`du`, stats) and a design that leaves room for future CUDA integration.

---

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Foundation: repo, CMake, Fedora env, minimal passthrough FS, on-disk layout & metadata design | Not started |
| **Phase 2** | Compression: write/read paths, getattr/readdir/unlink, single-threaded correctness | Not started |
| **Phase 3** | Multithreading, thread pool, stats (`.cfs_stats` or `cfsctl`), error handling | Not started |
| **Phase 4** | Polish: demo script, report/slides, optional caching, CUDA-ready interface | Not started |

**Overall:** Nothing implemented yet — project just starting.

---

## File structure

```
CrunchFS/
├── CMakeLists.txt
├── README.md
├── summary.txt
├── src/
│   ├── main.cpp              # Entry, FUSE mount
│   ├── fuse_ops.cpp/hpp      # FUSE callbacks
│   ├── metadata.cpp/hpp      # Path → chunks, size, timestamps
│   ├── chunk_store.cpp/hpp   # Backing-store I/O
│   ├── compression/
│   │   ├── interface.hpp     # compress_chunk / decompress_chunk API
│   │   └── zstd_backend.cpp/hpp
│   └── thread_pool.cpp/hpp   # Phase 3
├── include/crunchfs/         # Optional: config.hpp
├── tests/
└── docs/                     # Phase 4
```

---

## Prerequisites (Fedora)

- **FUSE**: `sudo dnf install fuse fuse-devel` (or `fuse3` / `fuse3-devel` for libfuse3)
- **Build**: `gcc`/`g++`, `cmake`, `make`
- **Compression** (when added): zstd or LZ4 — e.g. `sudo dnf install zstd zstd-devel` or `lz4 lz4-devel`

*(Exact package names may vary; will be updated when the build is set up.)*

---

## How to build

*To be added in Phase 1.*

```bash
# Placeholder — will be something like:
# mkdir build && cd build
# cmake ..
# make
```

---

## How to run

*To be added after Phase 1 (passthrough FS).*

```bash
# Placeholder — will be something like:
# mkdir -p /mnt/cfs
# ./crunchfs <backing_store_path> /mnt/cfs
# ... use /mnt/cfs ...
# fusermount -u /mnt/cfs
```

---

## Verification (when implemented)

- **Logical size:** `ls -lh /mnt/cfs/file` (e.g. 2 GB)
- **Compressed size:** `du -h <backing_store_path>`
- **Stats:** virtual file `/mnt/cfs/.cfs_stats` or CLI `cfsctl stats` (logical size, compressed size, ratio)

---

## Project plan

See **[summary.txt](summary.txt)** for the full phased plan, constraints, and CUDA-ready design notes.

---

## Team

3 people. Target: early April (e.g. 1 April).
