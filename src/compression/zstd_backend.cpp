#include "interface.hpp"
#include <zstd.h>

size_t compress_bound(size_t src_size) {
    return ZSTD_compressBound(src_size);
}

size_t compress_chunk(const void* src, size_t src_size,
                      void* dst, size_t dst_cap, int level) {
    size_t r = ZSTD_compress(dst, dst_cap, src, src_size, level);
    return ZSTD_isError(r) ? 0 : r;
}

size_t decompress_chunk(const void* src, size_t src_size,
                        void* dst, size_t dst_cap) {
    size_t r = ZSTD_decompress(dst, dst_cap, src, src_size);
    return ZSTD_isError(r) ? 0 : r;
}
