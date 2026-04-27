#pragma once
#include <cstddef>
#include <vector>

// Compress src → dst. Returns compressed size, or 0 on failure.
size_t compress_chunk(const void* src, size_t src_size,
                      void* dst, size_t dst_cap, int level = 3);

// Decompress src → dst. Returns decompressed size, or 0 on failure.
size_t decompress_chunk(const void* src, size_t src_size,
                        void* dst, size_t dst_cap);

// Upper-bound on compressed output for src_size bytes.
size_t compress_bound(size_t src_size);
