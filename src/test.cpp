#include "compresslib.h"
#include <iostream>
#include <cstring>

int main() {
    const char* text = "Hello world! This is a test for compression.";
    size_t size = strlen(text);

    // Compress
    auto chunks = compress_file_chunks(
        reinterpret_cast<const uint8_t*>(text),
        size,
        0
    );

    std::cout << "Compressed into " << chunks.size() << " chunk(s)\n";

    // Decompress
    auto decompressed = decompress_file_chunks(chunks, 0, size);

    std::string result(decompressed.begin(), decompressed.end());

    std::cout << "Decompressed: " << result << "\n";

    // Check
    if (result == text) {
        std::cout << "SUCCESS ✅\n";
    } else {
        std::cout << "FAIL ❌\n";
    }

    return 0;
}
