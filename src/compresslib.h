#pragma once
#include <vector>
#include <cstdint>

const size_t CHUNK_SIZE = 4096;

std::vector<std::vector<uint8_t>> compress_file_chunks(
    const uint8_t* data,
    size_t size,
    size_t offset
);

std::vector<uint8_t> decompress_file_chunks(
    const std::vector<std::vector<uint8_t>>& chunks,
    size_t offset,
    size_t size
);
