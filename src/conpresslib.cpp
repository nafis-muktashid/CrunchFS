#include "compresslib.h"
#include <zstd.h>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace std;

// -----------------------------
// Helper: compress one chunk
// -----------------------------
static vector<uint8_t> compress_chunk(const uint8_t* data, size_t size) {
    size_t max_compressed_size = ZSTD_compressBound(size);
    vector<uint8_t> compressed(max_compressed_size);

    size_t compressed_size = ZSTD_compress(
        compressed.data(),
        max_compressed_size,
        data,
        size,
        1
    );

    if (ZSTD_isError(compressed_size)) {
        throw runtime_error("ZSTD_compress failed");
    }

    compressed.resize(compressed_size);
    return compressed;
}

// -----------------------------
// Helper: decompress one chunk
// -----------------------------
static vector<uint8_t> decompress_chunk(const uint8_t* data, size_t size) {
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);

    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw runtime_error("Invalid compressed data");
    }

    vector<uint8_t> decompressed(decompressed_size);

    size_t result = ZSTD_decompress(
        decompressed.data(),
        decompressed_size,
        data,
        size
    );

    if (ZSTD_isError(result)) {
        throw runtime_error("ZSTD_decompress failed");
    }

    return decompressed;
}

// -----------------------------
// Main: compress file into chunks
// -----------------------------
vector<vector<uint8_t>> compress_file_chunks(
    const uint8_t* data,
    size_t size,
    size_t offset
) {
    vector<vector<uint8_t>> result;

    size_t bytes_processed = 0;
    size_t current_offset = offset;

    while (bytes_processed < size) {
        size_t offset_in_chunk = current_offset % CHUNK_SIZE;
        size_t space_in_chunk = CHUNK_SIZE - offset_in_chunk;
        size_t bytes_to_take = min(space_in_chunk, size - bytes_processed);

        const uint8_t* chunk_data = data + bytes_processed;

        result.push_back(compress_chunk(chunk_data, bytes_to_take));

        bytes_processed += bytes_to_take;
        current_offset += bytes_to_take;
    }

    return result;
}

// -----------------------------
// Main: decompress chunks into buffer
// -----------------------------
vector<uint8_t> decompress_file_chunks(
    const vector<vector<uint8_t>>& chunks,
    size_t offset,
    size_t size
) {
    vector<uint8_t> result;
    result.reserve(size);

    size_t bytes_collected = 0;
    size_t current_offset = offset;

    for (const auto& chunk : chunks) {
        if (bytes_collected >= size) break;

        vector<uint8_t> decompressed = decompress_chunk(
            chunk.data(),
            chunk.size()
        );

        size_t offset_in_chunk = current_offset % CHUNK_SIZE;
        size_t available = decompressed.size() - offset_in_chunk;
        size_t bytes_to_copy = min(available, size - bytes_collected);

        result.insert(
            result.end(),
            decompressed.begin() + offset_in_chunk,
            decompressed.begin() + offset_in_chunk + bytes_to_copy
        );

        bytes_collected += bytes_to_copy;
        current_offset += bytes_to_copy;
    }

    return result;
}
