#include "metadata.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <functional>

namespace fs = std::filesystem;

MetaStore::MetaStore(const std::string& backing_dir) {
    meta_dir_   = backing_dir + "/.crunchfs/meta";
    index_file_ = backing_dir + "/.crunchfs/index.txt";
    fs::create_directories(meta_dir_);
    load_index();
}

std::string MetaStore::hash_path(const std::string& p) {
    // Simple but stable: FNV-1a 64-bit → hex string
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : p) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h;
    return ss.str();
}

std::string MetaStore::meta_path(const std::string& path_id) const {
    return meta_dir_ + "/" + path_id + ".meta";
}

void MetaStore::load_index() {
    index_.clear();
    std::ifstream f(index_file_);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto sep = line.find('\t');
        if (sep == std::string::npos) continue;
        index_[line.substr(0, sep)] = line.substr(sep + 1);
    }
}

void MetaStore::save_index() {
    std::ofstream f(index_file_, std::ios::trunc);
    for (auto& [path, id] : index_)
        f << path << '\t' << id << '\n';
}

std::string MetaStore::path_to_id(const std::string& logical_path) {
    std::unique_lock lk(mu_);
    auto it = index_.find(logical_path);
    return it == index_.end() ? "" : it->second;
}

bool MetaStore::load(const std::string& path_id, FileMeta& out) {
    std::unique_lock lk(mu_);
    std::ifstream f(meta_path(path_id));
    if (!f) return false;
    std::string key; std::string val;
    while (f >> key >> val) {
        if (key == "logical_path")     out.logical_path    = val;
        else if (key == "path_id")     out.path_id         = val;
        else if (key == "logical_size")out.logical_size    = std::stoull(val);
        else if (key == "comp_size")   out.compressed_size = std::stoull(val);
        else if (key == "chunk_size")  out.chunk_size      = std::stoull(val);
        else if (key == "n_chunks")    out.n_chunks        = std::stoull(val);
        else if (key == "atime")       out.atime           = (time_t)std::stoll(val);
        else if (key == "mtime")       out.mtime           = (time_t)std::stoll(val);
        else if (key == "ctime")       out.ctime           = (time_t)std::stoll(val);
        else if (key == "mode")        out.mode            = (mode_t)std::stoul(val, nullptr, 8);
    }
    return true;
}

bool MetaStore::save(const FileMeta& m) {
    std::unique_lock lk(mu_);
    std::ofstream f(meta_path(m.path_id), std::ios::trunc);
    if (!f) return false;
    f << "path_id "      << m.path_id         << '\n'
      << "logical_path " << m.logical_path     << '\n'
      << "logical_size " << m.logical_size     << '\n'
      << "comp_size "    << m.compressed_size  << '\n'
      << "chunk_size "   << m.chunk_size       << '\n'
      << "n_chunks "     << m.n_chunks         << '\n'
      << "atime "        << (long long)m.atime << '\n'
      << "mtime "        << (long long)m.mtime << '\n'
      << "ctime "        << (long long)m.ctime << '\n'
      << "mode "         << std::oct << m.mode << '\n';
    return true;
}

bool MetaStore::remove(const std::string& path_id) {
    std::unique_lock lk(mu_);
    return fs::remove(meta_path(path_id));
}

void MetaStore::index_add(const std::string& logical_path, const std::string& path_id) {
    std::unique_lock lk(mu_);
    index_[logical_path] = path_id;
    save_index();
}

void MetaStore::index_remove(const std::string& logical_path) {
    std::unique_lock lk(mu_);
    index_.erase(logical_path);
    save_index();
}

std::vector<std::string> MetaStore::list_paths() {
    std::unique_lock lk(mu_);
    std::vector<std::string> out;
    out.reserve(index_.size());
    for (auto& [p, _] : index_) out.push_back(p);
    return out;
}

void MetaStore::accumulate_stats(size_t& total_logical, size_t& total_compressed) {
    std::unique_lock lk(mu_);
    total_logical = total_compressed = 0;
    for (auto& [path, id] : index_) {
        FileMeta m;
        std::ifstream f(meta_path(id));
        if (!f) continue;
        std::string key; std::string val;
        while (f >> key >> val) {
            if (key == "logical_size") total_logical     += std::stoull(val);
            if (key == "comp_size")    total_compressed  += std::stoull(val);
        }
    }
}
