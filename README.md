# CrunchFS

**Multithreaded transparent compressed FUSE-based filesystem** — CSE323 OS Project.

A FUSE filesystem on Fedora (C/C++) that transparently compresses file data in chunks using multithreading, with a clear way to show compression (`du`, stats) and a design that leaves room for future CUDA integration.

---

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Fedora + libfuse3, read-only passthrough in `src/`, CMake build; `fuse-test/` holds optional hello demo | **Done** (prototype) |
| **Phase 2** | Compression: write/read paths, chunking, backing layout, single-threaded correctness | Not started |
| **Phase 3** | Multithreading, thread pool, stats (`.cfs_stats` or `cfsctl`), error handling | Not started |
| **Phase 4** | Polish: demo script, report/slides, optional caching, CUDA-ready interface | Not started |

**Overall:** Passthrough source is **`src/passthrough_cfs.cpp`**. Build artifacts go under **`build/`** (gitignored).

---

## File structure

```
CrunchFS/
├── CMakeLists.txt
├── README.md
├── summary.txt
├── src/
│   └── passthrough_cfs.cpp   # Phase 1 read-only passthrough
├── fuse-test/
│   ├── README.md
│   └── hello_fs.cpp          # minimal FUSE demo (optional)
├── include/crunchfs/           # (planned)
├── tests/
└── docs/
```

---

## Prerequisites (Fedora)

```bash
sudo dnf install fuse3 fuse3-devel gcc-c++ cmake make pkgconf-pkg-config
```

- **Compression** (Phase 2+): e.g. `sudo dnf install zstd zstd-devel` or `lz4 lz4-devel`

---

## Build (recommended)

Out-of-source build keeps binaries out of the tree:

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

The `passthrough_cfs` binary is **`build/passthrough_cfs`**.

Manual compile (equivalent):

```bash
g++ -std=c++17 src/passthrough_cfs.cpp -lfuse3 -o passthrough_cfs
```

`FUSE_USE_VERSION` in source must match your installed libfuse3 (this tree uses 36 on current Fedora).

---

## Run passthrough

**Terminal A** (foreground):

```bash
mkdir -p /tmp/cfs_backing /tmp/cfs_mount
echo "hello" > /tmp/cfs_backing/a.txt
./build/passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f
```

**Terminal B**:

```bash
ls -la /tmp/cfs_mount
cat /tmp/cfs_mount/a.txt
```

Optional subdirectory check:

```bash
mkdir -p /tmp/cfs_backing/a/b
echo "nested" > /tmp/cfs_backing/a/b/c.txt
ls -la /tmp/cfs_mount/a/b
cat /tmp/cfs_mount/a/b/c.txt
```

---

## Unmount

- Foreground: **Ctrl+C**, or  
- `fusermount3 -u /tmp/cfs_mount` (or `fusermount -u` on older setups)

Verify: `mount | grep cfs_mount` should show nothing.

---

## Repository hygiene

Do **not** commit:

- Compiled binaries (`passthrough_cfs`, `hello_fs`, `*.o`, `*.obj`, `build/` output)
- IDE folders under `.gitignore`

If something was committed by mistake, remove from the index (keep local file if needed):

```bash
git rm --cached path/to/binary
```

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
