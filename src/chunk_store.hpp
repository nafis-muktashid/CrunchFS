#pragma once
#include <string>
#include <vector>
#include <mutex>

// Manages on-disk compressed chunk files under <backing>/.crunchfs/data/
// Chunk file: <data_dir>/<path_id>_<chunk_idx>.dat
class ChunkStore {
public:
    static constexpr size_t CHUNK_SIZE = 256 * 1024; // 256 KiB

    explicit ChunkStore(const std::string& backing_dir);

    // Write raw (uncompressed) data as chunk chunk_idx for path_id.
    // Returns compressed size written, or 0 on failure.
    size_t write_chunk(const std::string& path_id, size_t chunk_idx,
                       const void* data, size_t len);

    // Read and decompress chunk chunk_idx. Appends to out.
    // Returns decompressed bytes, or 0 on failure.
    size_t read_chunk(const std::string& path_id, size_t chunk_idx,
                      std::vector<char>& out);

    // Delete all chunks for path_id.
    void remove_chunks(const std::string& path_id, size_t n_chunks);
    // Delete one specific chunk for path_id.
    void remove_chunk(const std::string& path_id, size_t chunk_idx);

    // Rename: copy all chunk files from old_id → new_id, remove old.
    bool rename_chunks(const std::string& old_id, const std::string& new_id,
                       size_t n_chunks);

    // On-disk compressed size for a single chunk.
    size_t chunk_disk_size(const std::string& path_id, size_t chunk_idx);

private:
    std::string data_dir_;
    std::mutex  mu_;  // coarse lock; Phase 3 upgrades to per-chunk

    std::string chunk_path(const std::string& path_id, size_t idx) const;
};
