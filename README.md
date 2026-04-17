# CrunchFS

**Multithreaded transparent compressed FUSE-based filesystem** — CSE323 OS Project.

A FUSE filesystem on Fedora (C/C++) that transparently compresses file data in chunks using multithreading, with a clear way to show compression (`du`, stats) and a design that leaves room for future CUDA integration.

---

## Status

| Phase | Description | Status |
|-------|-------------|--------|
| **Phase 1** | Fedora + libfuse3, read-only passthrough in `src/`, CMake build; `fuse-test/` holds optional hello demo | **Done** (prototype) |
| **Phase 2** | Compression: write/read paths, chunking, backing layout, single-threaded correctness | **Done** (validated) |
| **Phase 3** | Multithreading, thread pool, stats (`.cfs_stats` or `cfsctl`), error handling | Not started |
| **Phase 4** | Polish: demo script, report/slides, optional caching, CUDA-ready interface | Not started |

**Overall:** Main source is **`src/passthrough_cfs.cpp`**. Current implementation stores logical files under **`<backing>/.crunchfs/meta/*.meta`** and **`<backing>/.crunchfs/data/*.dat`**. Build artifacts go under **`build/`** (gitignored).

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
    └── metadata-format-v1.md   # on-disk layout; contract for compression + FUSE
```

---

## Prerequisites (Fedora)

```bash
sudo dnf install fuse3 fuse3-devel gcc-c++ cmake make pkgconf-pkg-config
```

- **Compression** (Phase 2+): install zstd runtime + dev headers

```bash
sudo dnf install zstd libzstd-devel
```

---

## Build

Out-of-source build keeps binaries out of the tree:

```bash
mkdir -p build && cd build
cmake ..
cmake --build .
```

The `passthrough_cfs` binary is **`build/passthrough_cfs`**.

`FUSE_USE_VERSION` in source must match your installed libfuse3 (this tree uses 36 on current Fedora).

---

## Daemon command and options

CrunchFS runs as a FUSE daemon binary:

```bash
./build/passthrough_cfs <backing_store_path> <mount_point> [FUSE options]
```

Example:

```bash
./build/passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f -s
```

Common FUSE options used in this project:

- `-f` run in foreground (recommended for debugging and demo)
- `-s` single-thread mode (recommended for current Phase 2 correctness tests)
- `-d` FUSE debug logs (optional)

Runtime behavior:

- Mount path is the user-visible filesystem view.
- File content is stored in backing path under `.crunchfs/meta` and `.crunchfs/data`.
- Regular file operations are served by the daemon callbacks (`getattr`, `open`, `read`, `write`, `truncate`, `readdir`, `rename`, `unlink`, `utimens`, etc.).

---

## Run CrunchFS (single-thread test mode)

**Terminal A** (foreground):

```bash
mkdir -p /tmp/cfs_backing /tmp/cfs_mount
./build/passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f -s
```

**Terminal B** (sanity check mount):

```bash
mount | rg cfs_mount
ls -la /tmp/cfs_mount
```

---

## Phase 2 verification commands (copy/paste)

### Part 1: Basic write/read

```bash
echo "hello crunchfs" > /tmp/cfs_mount/a.txt
cat /tmp/cfs_mount/a.txt
ls -lh /tmp/cfs_mount/a.txt
```

### Part 2: Append + truncate

```bash
echo "more text" >> /tmp/cfs_mount/a.txt
cat /tmp/cfs_mount/a.txt
truncate -s 5 /tmp/cfs_mount/a.txt
cat /tmp/cfs_mount/a.txt
```

### Part 3: Verify compressed backing layout

```bash
ls -la /tmp/cfs_backing/.crunchfs
ls -la /tmp/cfs_backing/.crunchfs/meta
ls -la /tmp/cfs_backing/.crunchfs/data
du -h /tmp/cfs_backing/.crunchfs/data
```

Expected:
- user file appears at mount path (`/tmp/cfs_mount/...`)
- backing raw file may not exist as plain path
- `.crunchfs/meta/*.meta` and `.crunchfs/data/*.dat` should exist

### Part 4: Rename + delete

```bash
mv /tmp/cfs_mount/a.txt /tmp/cfs_mount/b.txt
cat /tmp/cfs_mount/b.txt
rm /tmp/cfs_mount/b.txt
```

### Part 5: Large-file compression demo

Highly compressible file:

```bash
dd if=/dev/zero of=/tmp/cfs_mount/big_zero.bin bs=1M count=100 status=progress
ls -lh /tmp/cfs_mount/big_zero.bin
du -h /tmp/cfs_backing/.crunchfs/data
```

Incompressible (or weakly compressible) file:

```bash
dd if=/dev/urandom of=/tmp/cfs_mount/big_rand.bin bs=1M count=50 status=progress
ls -lh /tmp/cfs_mount/big_rand.bin
du -h /tmp/cfs_backing/.crunchfs/data
```

What this shows:

- `/dev/zero` -> highly compressible data (many repeated bytes), so `.crunchfs/data` grows much less than logical size.
- `/dev/urandom` -> high-entropy random data, usually not compressible, so `.crunchfs/data` grows close to logical size.

Reproduce comparison in one clean run:

```bash
rm -f /tmp/cfs_mount/big_zero.bin /tmp/cfs_mount/big_rand.bin

dd if=/dev/zero of=/tmp/cfs_mount/big_zero.bin bs=1M count=100 status=none
echo "After zero file:"
ls -lh /tmp/cfs_mount/big_zero.bin
du -h /tmp/cfs_backing/.crunchfs/data

dd if=/dev/urandom of=/tmp/cfs_mount/big_rand.bin bs=1M count=100 status=none
echo "After adding random file:"
ls -lh /tmp/cfs_mount/big_rand.bin
du -h /tmp/cfs_backing/.crunchfs/data
```

---

## Persistence check (recommended)

```bash
sha256sum /tmp/cfs_mount/big_zero.bin
```

Unmount and remount in this order, then run the same hash again:

```bash
fusermount3 -u /tmp/cfs_mount
./build/passthrough_cfs /tmp/cfs_backing /tmp/cfs_mount -f -s
```

In another terminal:

```bash
sha256sum /tmp/cfs_mount/big_zero.bin
```

Hashes should match.

---

## Stop / unmount

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

## Verification targets

- **Logical size:** `ls -lh <mountpoint>/file`
- **Compressed size:** `du -h <backing_store_path>/.crunchfs/data`
- **Integrity:** `sha256sum <mountpoint>/file` before and after remount
- **Stats endpoint:** `/mnt/cfs/.cfs_stats` or `cfsctl stats` (planned for Phase 3)

---

## Documentation

- **[docs/metadata-format-v1.md](docs/metadata-format-v1.md)** — Binary `.meta` / `.dat` layout, chunk table, zstd, path → `file_id`, `read`/`write`/`getattr` rules, threading notes for Phase 3, and suggested compression API surface for implementers.

---

## Project plan

See **[summary.txt](summary.txt)** for the full phased plan, constraints, and CUDA-ready design notes.

---