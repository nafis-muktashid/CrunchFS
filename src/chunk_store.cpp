#include "chunk_store.hpp"
#include "compression/interface.hpp"
#include <fstream>
#include <filesystem>
#include <vector>
#include <cstring>

namespace fs = std::filesystem;

ChunkStore::ChunkStore(const std::string& backing_dir) {
    data_dir_ = backing_dir + "/.crunchfs/data";
    fs::create_directories(data_dir_);
}

std::string ChunkStore::chunk_path(const std::string& path_id, size_t idx) const {
    return data_dir_ + "/" + path_id + "_" + std::to_string(idx) + ".dat";
}

size_t ChunkStore::write_chunk(const std::string& path_id, size_t chunk_idx,
                               const void* data, size_t len) {
    size_t cap = compress_bound(len);
    std::vector<char> buf(cap);
    size_t csz = compress_chunk(data, len, buf.data(), cap);
    if (csz == 0) return 0;

    std::unique_lock lk(mu_);
    std::ofstream f(chunk_path(path_id, chunk_idx),
                    std::ios::binary | std::ios::trunc);
    if (!f) return 0;
    f.write(buf.data(), (std::streamsize)csz);
    return csz;
}

size_t ChunkStore::read_chunk(const std::string& path_id, size_t chunk_idx,
                              std::vector<char>& out) {
    std::string cp;
    {
        std::unique_lock lk(mu_);
        cp = chunk_path(path_id, chunk_idx);
    }
    std::ifstream f(cp, std::ios::binary | std::ios::ate);
    if (!f) return 0;
    size_t csz = (size_t)f.tellg();
    f.seekg(0);
    std::vector<char> cbuf(csz);
    f.read(cbuf.data(), (std::streamsize)csz);

    // Decompress into a temporary buffer sized to max chunk
    std::vector<char> tmp(CHUNK_SIZE * 2);
    size_t dsz = decompress_chunk(cbuf.data(), csz, tmp.data(), tmp.size());
    if (dsz == 0) return 0;
    out.insert(out.end(), tmp.begin(), tmp.begin() + dsz);
    return dsz;
}

void ChunkStore::remove_chunks(const std::string& path_id, size_t n_chunks) {
    std::unique_lock lk(mu_);
    for (size_t i = 0; i < n_chunks; ++i)
        fs::remove(chunk_path(path_id, i));
}

bool ChunkStore::rename_chunks(const std::string& old_id, const std::string& new_id,
                               size_t n_chunks) {
    std::unique_lock lk(mu_);
    for (size_t i = 0; i < n_chunks; ++i) {
        std::string src = chunk_path(old_id, i);
        std::string dst = chunk_path(new_id, i);
        std::error_code ec;
        fs::rename(src, dst, ec);
        if (ec) return false;
    }
    return true;
}

size_t ChunkStore::chunk_disk_size(const std::string& path_id, size_t chunk_idx) {
    std::unique_lock lk(mu_);
    std::error_code ec;
    auto sz = fs::file_size(chunk_path(path_id, chunk_idx), ec);
    return ec ? 0 : (size_t)sz;
}
