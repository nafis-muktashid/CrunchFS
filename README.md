# CrunchFS-2

Transparent compressed FUSE filesystem with modular internals and multithreaded chunk reads.

## Status

| Phase | Scope | Status |
|---|---|---|
| Phase 1 | FUSE daemon + passthrough baseline | Done |
| Phase 2 | Compressed logical file storage + correctness | Done |
| Phase 3 | Modularization + thread pool + stats endpoint | Done |
| Phase 4 | Optional polish (cache/CUDA/report extras) | Optional |

## Current Architecture

- `src/main.cpp` boots FUSE and prepares backing directories.
- `src/fuse_ops.cpp` contains FUSE callbacks and orchestration logic.
- `src/metadata.cpp` stores metadata/index under `<backing>/.crunchfs/meta` and `<backing>/.crunchfs/index.txt`.
- `src/chunk_store.cpp` stores compressed chunks under `<backing>/.crunchfs/data`.
- `src/thread_pool.cpp` provides worker threads used for parallel chunk reads.
- `src/conpresslib.cpp` + `src/compresslib.h` are the only active compression backend (zstd).

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Binary: `build/passthrough_cfs`

## Run

```bash
mkdir -p /tmp/cfs2_backing /tmp/cfs2_mount
./build/passthrough_cfs /tmp/cfs2_backing /tmp/cfs2_mount -f
```

- Keep `-f` for foreground demo.
- Do not pass `-s` if you want multithreaded request handling.
- Add `-d` only when you want verbose FUSE logs.

## One-command Demo

Run:

```bash
./demo.sh
```

The script verifies:
- mount readiness,
- basic create/read/write/truncate/rename/unlink,
- backing layout under `.crunchfs`,
- compression behavior (`/dev/zero` vs `/dev/urandom`),
- `.cfs_stats` endpoint,
- concurrent workload smoke test,
- persistence hash before/after remount.

## Manual Proof Points

- Compression: compare `ls -lh /tmp/cfs2_mount/*.bin` with `du -h /tmp/cfs2_backing/.crunchfs/data`.
- Stats: `cat /tmp/cfs2_mount/.cfs_stats`.
- Persistence: `sha256sum` before and after remount.
- Concurrency smoke: run two `dd` writes/readers in parallel to mounted files.

## Notes

- Storage is persistent in the chosen backing path, not in `build/`.
- Removing mount/backing directories deletes runtime data; recreate them before rerun.