# CrunchFS — On-disk metadata format (version 1)

This document is the **contract** between:

- **FUSE layer** (logical paths, `stat` size, `read`/`write` offsets),
- **Compression / storage** (zstd, chunking, serialization),
- **Threading** (Phase 3: what to lock, what is per-file).

Implementations **must** bump the format `version` field if any binary layout below changes. Readers should reject unknown versions with a clear error.

---

## 1. Goals

- Expose **normal POSIX file semantics** through FUSE: logical file size, byte-addressable `read`/`write`.
- Store file contents as **fixed-size logical chunks** (except the last), each **compressed** with **zstd** on disk.
- Keep enough metadata to map **logical offset** → **chunk index** → **compressed blob location** without scanning the whole dataset.

---

## 2. Constants (v1)

| Name | Value | Notes |
|------|--------|--------|
| **Logical chunk size** | `CRUNCH_CHUNK_SIZE = 4 * 1024 * 1024` (4 MiB) | Fixed for v1. Last chunk may be shorter. |
| **Codec** | **zstd** | Default compression level is implementation-defined (e.g. `3`); store **codec id** in metadata so future codecs are possible. |
| **Codec id** | `1` = zstd | `0` = none/raw (optional, for debugging). |
| **Byte order** | **Little-endian** | All multi-byte integers below are LE (`uint16_t`–`uint64_t`). |
| **Path encoding** | UTF-8 | For any path string stored in metadata (v1 uses hashed filenames; see §4). |

---

## 3. Backing store layout (under the directory passed to CrunchFS)

The user (or init script) chooses a **backing root** directory, e.g. `/var/crunchfs-backing` or `/tmp/cfs_backing`. CrunchFS **must not** store user-visible filenames as a single flat string if that risks `..` or `/` issues; v1 uses a **content-addressed or hashed layout** under a hidden tree.

### 3.1 Directory tree (v1)

```
<backing_root>/
└── .crunchfs/
    ├── version          # optional: single ASCII line "1\n" for whole store (optional)
    ├── meta/            # per-file metadata files (see §5)
    │   └── <file_id>.meta
    └── data/            # per-file compressed chunk blobs (see §6)
        └── <file_id>.dat
```

- **`<file_id>`**: Deterministic identifier for a **logical file** inside the mount. v1 recommendation:

  `file_id = lowercase hex(SHA-256(UTF-8(normalized_mount_path)))` truncated or full 64 hex chars.

  Where `normalized_mount_path` is the path **as seen by FUSE**, e.g. `/notes/a.txt`, normalized:

  - No trailing slash (except root `/` special-cased),
  - Collapse duplicate `/`,
  - Reject `..` components at map time (FUSE should resolve or reject).

  **Why:** Avoids filesystem limits on filename length and weird characters on disk.

- **Root directory `/`**: Directory metadata may use a reserved id, e.g. `file_id = "root"` or a fixed hash of `"/"` — team must handle **directories** vs **files** in `getattr`/`readdir` (directories may be implicit: only paths that appear in a listing). *If v1 only supports files under a tree, state that explicitly in code.*

### 3.2 What the compression owner must implement

- Create/read/update **`<backing_root>/.crunchfs/meta/<file_id>.meta`** and **`<backing_root>/.crunchfs/data/<file_id>.dat`** atomically where possible (write temp + `rename`).

---

## 4. Mapping FUSE path → on-disk files

| FUSE path (examples) | Action |
|----------------------|--------|
| `/foo/bar.txt` | Compute `file_id` from `/foo/bar.txt` → open `meta/<file_id>.meta` and `data/<file_id>.dat`. |
| Directory operations | v1 may store **only file** bodies compressed; directories can be **synthetic** (readdir walks known prefixes or a separate index). *If minimal scope: support flat list of files only; document limitation.* |

**Open question for implementation (document in code):** whether you maintain a **path → file_id index** (SQLite, or a small `index` file) for fast readdir. v1 **does not** mandate an index; a simple approach is storing **parent id + name** in an optional sidecar (future v2). For **course scope**, listing may be **slow** or limited — still document behavior.

---

## 5. Per-file metadata file (binary): `<file_id>.meta`

### 5.1 File header (fixed order)

| Offset | Size | Type | Field | Description |
|--------|------|------|--------|-------------|
| 0 | 4 | `char[4]` | `magic` | Literal ASCII **`CFM1`** (0x43 0x46 0x4D 0x31). Invalid → not a CrunchFS meta file. |
| 4 | 1 | `uint8_t` | `format_version` | **1** for this document. |
| 5 | 1 | `uint8_t` | `codec_id` | **1** = zstd. |
| 6 | 2 | `uint16_t` | `reserved` | **0** for v1. |
| 8 | 8 | `uint64_t` | `logical_size` | Size in bytes that **`stat` and read** must expose (uncompressed). |
| 16 | 4 | `uint32_t` | `chunk_size` | Must equal **`CRUNCH_CHUNK_SIZE`** (4194304) in v1. |
| 20 | 4 | `uint32_t` | `chunk_count` | Number of **chunk entries** following. |
| 24 | 4 | `uint32_t` | `zstd_level` | Compression level used (informational; 0 = default). |
| 28 | 4 | `uint32_t` | `flags` | **0** for v1. Bit 0 could mean “encrypted” in future. |
| 32 | 32 | `uint8_t[32]` | `content_sha256` | Optional: SHA-256 of **logical** (uncompressed) content; **0** if not computed. |

**Header size:** **64 bytes** for v1.

### 5.2 Chunk table (repeated `chunk_count` times)

Each entry describes **one** logical chunk in order (chunk index `0 .. chunk_count-1`).

| Size | Type | Field | Description |
|------|------|--------|-------------|
| 4 | `uint32_t` | `chunk_index` | Must equal row index (0,1,2,…) for v1; allows sanity checks. |
| 4 | `uint32_t` | `logical_length` | Uncompressed length for this chunk. **Usually** `chunk_size`, except last chunk (`≤ chunk_size`). |
| 8 | `uint64_t` | `data_offset` | Byte offset in **`<file_id>.dat`** where this chunk’s **compressed** blob starts. |
| 4 | `uint32_t` | `compressed_size` | Length in bytes of compressed blob on disk. |
| 4 | `uint32_t` | `reserved` | **0**. |

**Per-entry size:** **24 bytes**.

**Total meta file size:** `64 + chunk_count * 24` bytes.

### 5.3 Invariants

- `logical_size` = sum of all `logical_length` fields (must hold).
- Chunks are **contiguous in logical space**: chunk `i` covers logical bytes `[i * chunk_size, min((i+1)*chunk_size, logical_size))` for non-last; last chunk shorter.
- `data_offset` + `compressed_size` for each chunk must not overlap the next chunk’s range in `.dat` if blobs are packed sequentially (recommended: pack in order with no gaps for simplicity).

---

## 6. Per-file data file: `<file_id>.dat`

- **Opaque sequence of compressed blobs**, one per chunk, **in chunk index order** (chunk 0 first).
- **Recommended v1 layout:** store blobs **back-to-back** in order; `data_offset` in meta points into this file (first chunk often at offset `0`).
- **Alternative:** single offset table only in meta (as above); no extra headers inside `.dat` required for v1.

**Decompression:** For `read(offset, length)`, determine chunk indices covering `[offset, offset+length)`, seek each blob via `data_offset`, read `compressed_size` bytes, **zstd decompress** into a buffer of `logical_length` bytes, then copy the requested slice.

---

## 7. `getattr` (stat)

- `st_size` = `logical_size` from `.meta`.
- `st_mode`: regular file `S_IFREG` as appropriate; mtime/ctime may come from separate extended attributes or `.meta` extension in v2; v1 may use backing file mtime of `.meta`.

---

## 8. `read` (logical)

1. Clamp `offset` / `length` to `[0, logical_size)`.
2. For each overlapping chunk index, load chunk metadata row, read compressed region from `.dat`, decompress, copy relevant bytes into user buffer.

**Caching (optional):** LRU of decompressed chunks — document if implemented; important for Member 3.

---

## 9. `write` (logical)

Writes are **not** required to align to 4 MiB boundaries.

**Recommended strategy for v1:**

- Maintain an **in-memory write buffer** per open file (or global staging) that appends until:
  - a full **4 MiB** logical chunk is formed → compress with zstd → append to `.dat`, append row to chunk table, update `logical_size` and `chunk_count`; **or**
  - `fsync` / `release` → flush partial tail as final chunk (`logical_length` < 4 MiB).

**Concurrency:** Until Phase 3, **serialize writes per file** (one writer or lock).

**Truncation:** If `logical_size` shrinks, drop chunk rows and truncate `.dat` accordingly; if extending, may create a hole (zero-filled) or explicit sparse representation — v1 simplest: **rewrite** tail metadata and data (acceptable for course).

---

## 10. Unlink / rename

- **Unlink:** Delete `meta/<file_id>.meta` and `data/<file_id>.dat` (and any cache entries).
- **Rename:** Either **copy** meta+data to new `file_id` and delete old, or maintain rename table (v2); v1 **copy + delete** is simplest.

---

## 11. Verification / stats (for course demo)

- **Logical size:** `logical_size` in `.meta` (matches `ls -l`).
- **Compressed footprint:** `du` on `.crunchfs/data/<file_id>.dat` (and meta) vs logical size — **compression ratio** for reports.

Optional global stats file (Phase 3): e.g. `.crunchfs/stats` regenerated on demand — **not part of v1 meta format**.

---

## 12. Threading (Phase 3 — Member 3)

| Resource | Suggestion |
|----------|------------|
| **Per-file `.meta` / `.dat`** | One **mutex per `file_id`** or per open `fuse_file_info` private handle for read/write/decompress. |
| **Chunk decompress** | Parallelize **different chunk indices** for the same read only if buffers don’t race; **do not** parallelize two writes to same file without locking. |
| **Global structures** | If a global chunk allocator or free list exists, **global lock** or lock-free design — document actual choice. |

**Single-threaded Phase 2:** No locks required if FUSE runs `-s` or single worker; still design APIs so adding locks is localized.

---

## 13. Error handling expectations

- **Corrupt meta:** Wrong `magic` or failed invariant → return `EIO` (or `-EIO`) to FUSE.
- **zstd decompress error:** Treat as `EIO` for that read.
- **Missing .dat chunk:** `EIO`.

---

## 14. Compression API (informative — for Member 2)

Suggested C++-style surface (names arbitrary; **signatures** matter):

```text
// Returns compressed size written to out, or 0 / negative on failure.
ssize_t compress_chunk_zstd(const void* src, size_t src_len,
                            std::vector<std::byte>& out, int level);

// Decompresses one chunk; dst must be at least logical_length bytes.
bool decompress_chunk_zstd(const void* src, size_t compressed_len,
                           void* dst, size_t dst_len);
```

- **Input chunk** for compression is **uncompressed** bytes of length `logical_length` (≤ `CRUNCH_CHUNK_SIZE`).
- **Output** is stored in `.dat` as raw zstd frame (no extra header required if `compressed_size` is in meta).

---

## 15. Document history

| Version | Date | Changes |
|---------|------|--------|
| 1 | (project) | Initial v1: CFM1 header, chunk table, `.meta`/`.dat`, zstd, 4 MiB chunks, LE. |

---

## 16. Quick reference — numbers

- Magic: **`CFM1`**
- `format_version`: **1**
- `chunk_size`: **4194304**
- `codec_id`: **1** (zstd)
- Header: **64** bytes  
- Chunk entry: **24** bytes  

---

*End of metadata-format-v1.*
