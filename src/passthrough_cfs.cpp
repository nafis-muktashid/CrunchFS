#define FUSE_USE_VERSION 36

#include <fuse3/fuse.h>

#include "compresslib.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

struct cfs_config {
    std::string backing_dir;
};

struct ChunkMeta {
    uint32_t logical_length;
    uint64_t data_offset;
    uint32_t compressed_size;
};

struct FileMeta {
    uint64_t logical_size = 0;
    std::vector<ChunkMeta> chunks;
};

struct OpenFileHandle {
    std::string path;
    std::vector<uint8_t> data;
    bool dirty = false;
};

static const char META_MAGIC[4] = {'C', 'F', 'M', '1'};
static constexpr uint8_t META_VERSION = 1;
static constexpr uint8_t META_CODEC_ID_ZSTD = 1;
static constexpr size_t META_HEADER_SIZE = 64;
static constexpr size_t META_ENTRY_SIZE = 24;

static const cfs_config* get_cfg() {
    return static_cast<const cfs_config*>(fuse_get_context()->private_data);
}

static std::string normalize_path(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return "/";
    }
    std::string p(path);
    if (p[0] != '/') {
        p = "/" + p;
    }
    std::string out;
    out.reserve(p.size());
    bool prev_slash = false;
    for (char ch : p) {
        if (ch == '/') {
            if (!prev_slash) {
                out.push_back(ch);
            }
            prev_slash = true;
        } else {
            out.push_back(ch);
            prev_slash = false;
        }
    }
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out.empty() ? "/" : out;
}

static std::string to_backing_path(const cfs_config* cfg, const char* fuse_path) {
    std::string normalized = normalize_path(fuse_path);
    if (normalized == "/") {
        return cfg->backing_dir;
    }
    return cfg->backing_dir + normalized;
}

static std::string crunch_root(const cfs_config* cfg) {
    return cfg->backing_dir + "/.crunchfs";
}

static std::string meta_dir(const cfs_config* cfg) {
    return crunch_root(cfg) + "/meta";
}

static std::string data_dir(const cfs_config* cfg) {
    return crunch_root(cfg) + "/data";
}

static std::string index_path(const cfs_config* cfg) {
    return crunch_root(cfg) + "/index.txt";
}

static std::string file_id_from_path(const std::string& normalized_path) {
    // Deterministic id for v1; replace with SHA-256 for stronger guarantees later.
    uint64_t h = std::hash<std::string>{}(normalized_path);
    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << h;
    return oss.str();
}

static std::string meta_path_for_id(const cfs_config* cfg, const std::string& file_id) {
    return meta_dir(cfg) + "/" + file_id + ".meta";
}

static std::string data_path_for_id(const cfs_config* cfg, const std::string& file_id) {
    return data_dir(cfg) + "/" + file_id + ".dat";
}

static std::string meta_path_for_fuse_path(const cfs_config* cfg, const std::string& fuse_path) {
    return meta_path_for_id(cfg, file_id_from_path(fuse_path));
}

static std::string data_path_for_fuse_path(const cfs_config* cfg, const std::string& fuse_path) {
    return data_path_for_id(cfg, file_id_from_path(fuse_path));
}

template <typename T>
static bool write_le(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return out.good();
}

template <typename T>
static bool read_le(std::ifstream& in, T* value) {
    in.read(reinterpret_cast<char*>(value), sizeof(T));
    return in.good();
}

static bool ensure_storage_layout(const cfs_config* cfg) {
    std::error_code ec;
    fs::create_directories(meta_dir(cfg), ec);
    if (ec) {
        return false;
    }
    fs::create_directories(data_dir(cfg), ec);
    if (ec) {
        return false;
    }
    std::ofstream touch(index_path(cfg), std::ios::app);
    return touch.good();
}

static bool load_index(const cfs_config* cfg, std::set<std::string>* out) {
    out->clear();
    std::ifstream in(index_path(cfg));
    if (!in.is_open()) {
        return true;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            out->insert(normalize_path(line.c_str()));
        }
    }
    return in.good() || in.eof();
}

static bool save_index_atomic(const cfs_config* cfg, const std::set<std::string>& entries) {
    std::string tmp_path = index_path(cfg) + ".tmp";
    std::ofstream out(tmp_path, std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    for (const std::string& path : entries) {
        out << path << "\n";
    }
    out.close();
    if (!out.good()) {
        return false;
    }
    return ::rename(tmp_path.c_str(), index_path(cfg).c_str()) == 0;
}

static bool add_index_entry(const cfs_config* cfg, const std::string& path) {
    std::set<std::string> entries;
    if (!load_index(cfg, &entries)) {
        return false;
    }
    entries.insert(path);
    return save_index_atomic(cfg, entries);
}

static bool remove_index_entry(const cfs_config* cfg, const std::string& path) {
    std::set<std::string> entries;
    if (!load_index(cfg, &entries)) {
        return false;
    }
    entries.erase(path);
    return save_index_atomic(cfg, entries);
}

static bool logical_file_exists(const cfs_config* cfg, const std::string& path) {
    return fs::exists(meta_path_for_fuse_path(cfg, path));
}

static bool load_meta(const cfs_config* cfg, const std::string& path, FileMeta* meta_out) {
    std::ifstream in(meta_path_for_fuse_path(cfg, path), std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    char magic[4];
    in.read(magic, sizeof(magic));
    if (!in.good() || std::memcmp(magic, META_MAGIC, sizeof(magic)) != 0) {
        return false;
    }

    uint8_t version = 0;
    uint8_t codec_id = 0;
    uint16_t reserved16 = 0;
    uint32_t chunk_size = 0;
    uint32_t chunk_count = 0;
    uint32_t zstd_level = 0;
    uint32_t flags = 0;
    uint8_t digest[32] = {};

    if (!read_le(in, &version) || !read_le(in, &codec_id) || !read_le(in, &reserved16) ||
        !read_le(in, &meta_out->logical_size) || !read_le(in, &chunk_size) ||
        !read_le(in, &chunk_count) || !read_le(in, &zstd_level) || !read_le(in, &flags)) {
        return false;
    }
    in.read(reinterpret_cast<char*>(digest), sizeof(digest));
    if (!in.good() || version != META_VERSION || codec_id != META_CODEC_ID_ZSTD ||
        chunk_size != CHUNK_SIZE) {
        return false;
    }

    meta_out->chunks.clear();
    meta_out->chunks.reserve(chunk_count);
    for (uint32_t i = 0; i < chunk_count; ++i) {
        uint32_t chunk_index = 0;
        ChunkMeta cm{};
        uint32_t reserved = 0;
        if (!read_le(in, &chunk_index) || !read_le(in, &cm.logical_length) ||
            !read_le(in, &cm.data_offset) || !read_le(in, &cm.compressed_size) ||
            !read_le(in, &reserved)) {
            return false;
        }
        if (chunk_index != i) {
            return false;
        }
        meta_out->chunks.push_back(cm);
    }

    uint64_t logical_sum = 0;
    for (const ChunkMeta& cm : meta_out->chunks) {
        logical_sum += cm.logical_length;
    }
    return logical_sum == meta_out->logical_size;
}

static bool save_meta_atomic(const cfs_config* cfg, const std::string& path, const FileMeta& meta) {
    std::string target = meta_path_for_fuse_path(cfg, path);
    std::string tmp = target + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out.write(META_MAGIC, sizeof(META_MAGIC));
    uint8_t version = META_VERSION;
    uint8_t codec_id = META_CODEC_ID_ZSTD;
    uint16_t reserved16 = 0;
    uint32_t chunk_size = CHUNK_SIZE;
    uint32_t chunk_count = static_cast<uint32_t>(meta.chunks.size());
    uint32_t zstd_level = 1;
    uint32_t flags = 0;
    uint8_t digest[32] = {};

    if (!write_le(out, version) || !write_le(out, codec_id) || !write_le(out, reserved16) ||
        !write_le(out, meta.logical_size) || !write_le(out, chunk_size) ||
        !write_le(out, chunk_count) || !write_le(out, zstd_level) || !write_le(out, flags)) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(digest), sizeof(digest));
    if (!out.good()) {
        return false;
    }

    for (uint32_t i = 0; i < chunk_count; ++i) {
        uint32_t idx = i;
        uint32_t row_reserved = 0;
        const ChunkMeta& cm = meta.chunks[i];
        if (!write_le(out, idx) || !write_le(out, cm.logical_length) || !write_le(out, cm.data_offset) ||
            !write_le(out, cm.compressed_size) || !write_le(out, row_reserved)) {
            return false;
        }
    }

    out.close();
    if (!out.good()) {
        return false;
    }
    return ::rename(tmp.c_str(), target.c_str()) == 0;
}

static bool persist_logical_file(const cfs_config* cfg, const std::string& path,
                                 const std::vector<uint8_t>& logical_data) {
    auto chunks = compress_file_chunks(logical_data.data(), logical_data.size(), 0);

    std::string data_target = data_path_for_fuse_path(cfg, path);
    std::string data_tmp = data_target + ".tmp";
    std::ofstream data_out(data_tmp, std::ios::binary | std::ios::trunc);
    if (!data_out.is_open()) {
        return false;
    }

    FileMeta meta;
    meta.logical_size = logical_data.size();
    meta.chunks.reserve(chunks.size());

    uint64_t offset = 0;
    size_t consumed = 0;
    for (const auto& chunk : chunks) {
        data_out.write(reinterpret_cast<const char*>(chunk.data()),
                       static_cast<std::streamsize>(chunk.size()));
        if (!data_out.good()) {
            return false;
        }
        ChunkMeta cm{};
        cm.logical_length = static_cast<uint32_t>(
            std::min(static_cast<size_t>(CHUNK_SIZE), logical_data.size() - consumed));
        cm.data_offset = offset;
        cm.compressed_size = static_cast<uint32_t>(chunk.size());
        meta.chunks.push_back(cm);
        consumed += cm.logical_length;
        offset += chunk.size();
    }

    data_out.close();
    if (!data_out.good()) {
        return false;
    }
    if (::rename(data_tmp.c_str(), data_target.c_str()) != 0) {
        return false;
    }
    if (!save_meta_atomic(cfg, path, meta)) {
        return false;
    }
    return add_index_entry(cfg, path);
}

static bool load_logical_file(const cfs_config* cfg, const std::string& path,
                              std::vector<uint8_t>* data_out) {
    FileMeta meta;
    if (!load_meta(cfg, path, &meta)) {
        return false;
    }

    std::ifstream data_in(data_path_for_fuse_path(cfg, path), std::ios::binary);
    if (!data_in.is_open()) {
        return false;
    }

    std::vector<std::vector<uint8_t>> compressed_chunks;
    compressed_chunks.reserve(meta.chunks.size());
    for (const ChunkMeta& cm : meta.chunks) {
        data_in.seekg(static_cast<std::streamoff>(cm.data_offset), std::ios::beg);
        if (!data_in.good()) {
            return false;
        }
        std::vector<uint8_t> compressed(cm.compressed_size);
        data_in.read(reinterpret_cast<char*>(compressed.data()),
                     static_cast<std::streamsize>(compressed.size()));
        if (!data_in.good()) {
            return false;
        }
        compressed_chunks.push_back(std::move(compressed));
    }

    *data_out = decompress_file_chunks(compressed_chunks, 0, meta.logical_size);
    return true;
}

static int flush_handle_if_dirty(const cfs_config* cfg, OpenFileHandle* h) {
    if (!h->dirty) {
        return 0;
    }
    if (!persist_logical_file(cfg, h->path, h->data)) {
        return -EIO;
    }
    h->dirty = false;
    return 0;
}

static int cfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void)fi;
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    std::string real = to_backing_path(cfg, path);

    memset(stbuf, 0, sizeof(struct stat));

    if (normalized == "/" || fs::is_directory(real)) {
        if (lstat(real.c_str(), stbuf) == -1) {
            return -errno;
        }
        return 0;
    }

    FileMeta meta;
    if (load_meta(cfg, normalized, &meta)) {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = static_cast<off_t>(meta.logical_size);
        stbuf->st_uid = getuid();
        stbuf->st_gid = getgid();
        struct stat meta_st {};
        std::string meta_path = meta_path_for_fuse_path(cfg, normalized);
        if (lstat(meta_path.c_str(), &meta_st) == 0) {
            stbuf->st_atim = meta_st.st_atim;
            stbuf->st_mtim = meta_st.st_mtim;
            stbuf->st_ctim = meta_st.st_ctim;
            stbuf->st_blksize = meta_st.st_blksize;
            stbuf->st_blocks = meta_st.st_blocks;
        }
        return 0;
    }

    if (lstat(real.c_str(), stbuf) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_readlink(const char* path, char* buf, size_t size) {
    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    ssize_t len = readlink(real.c_str(), buf, size - 1);
    if (len == -1) {
        return -errno;
    }
    buf[len] = '\0';
    return 0;
}

static int cfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    if (S_ISFIFO(mode)) {
        if (mkfifo(real.c_str(), mode & ~S_IFMT) == -1) {
            return -errno;
        }
    } else {
        if (mknod(real.c_str(), mode, rdev) == -1) {
            return -errno;
        }
    }
    return 0;
}

static int cfs_mkdir(const char* path, mode_t mode) {
    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    if (::mkdir(real.c_str(), mode) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_unlink(const char* path) {
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    std::string real = to_backing_path(cfg, path);

    if (logical_file_exists(cfg, normalized)) {
        std::error_code ec;
        fs::remove(meta_path_for_fuse_path(cfg, normalized), ec);
        if (ec) {
            return -EIO;
        }
        fs::remove(data_path_for_fuse_path(cfg, normalized), ec);
        if (ec) {
            return -EIO;
        }
        if (!remove_index_entry(cfg, normalized)) {
            return -EIO;
        }
        return 0;
    }

    if (::unlink(real.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_rmdir(const char* path) {
    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    if (::rmdir(real.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_symlink(const char* target, const char* linkpath) {
    auto* cfg = get_cfg();
    std::string real_link = to_backing_path(cfg, linkpath);
    if (::symlink(target, real_link.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_rename(const char* from, const char* to, unsigned int flags) {
    auto* cfg = get_cfg();
    std::string from_norm = normalize_path(from);
    std::string to_norm = normalize_path(to);
    std::string real_from = to_backing_path(cfg, from);
    std::string real_to = to_backing_path(cfg, to);

    if (flags != 0) {
        return -EINVAL;
    }

    if (logical_file_exists(cfg, from_norm)) {
        std::error_code ec;
        if (logical_file_exists(cfg, to_norm)) {
            fs::remove(meta_path_for_fuse_path(cfg, to_norm), ec);
            if (ec) {
                return -EIO;
            }
            fs::remove(data_path_for_fuse_path(cfg, to_norm), ec);
            if (ec) {
                return -EIO;
            }
        }
        if (::rename(meta_path_for_fuse_path(cfg, from_norm).c_str(),
                     meta_path_for_fuse_path(cfg, to_norm).c_str()) != 0) {
            return -errno;
        }
        if (::rename(data_path_for_fuse_path(cfg, from_norm).c_str(),
                     data_path_for_fuse_path(cfg, to_norm).c_str()) != 0) {
            return -errno;
        }
        if (!remove_index_entry(cfg, from_norm) || !add_index_entry(cfg, to_norm)) {
            return -EIO;
        }
        return 0;
    }

    if (::rename(real_from.c_str(), real_to.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_link(const char* from, const char* to) {
    auto* cfg = get_cfg();
    std::string real_from = to_backing_path(cfg, from);
    std::string real_to = to_backing_path(cfg, to);
    if (::link(real_from.c_str(), real_to.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_chmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void)fi;
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    if (logical_file_exists(cfg, normalized)) {
        return 0;
    }
    std::string real = to_backing_path(cfg, path);
    if (chmod(real.c_str(), mode) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi) {
    (void)fi;
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    if (logical_file_exists(cfg, normalized)) {
        return 0;
    }
    std::string real = to_backing_path(cfg, path);
    if (lchown(real.c_str(), uid, gid) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
    if (size < 0) {
        return -EINVAL;
    }
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);

    if (fi != nullptr && fi->fh != 0) {
        auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
        h->data.resize(static_cast<size_t>(size), 0);
        h->dirty = true;
        return 0;
    }

    if (logical_file_exists(cfg, normalized)) {
        std::vector<uint8_t> data;
        if (!load_logical_file(cfg, normalized, &data)) {
            return -EIO;
        }
        data.resize(static_cast<size_t>(size), 0);
        if (!persist_logical_file(cfg, normalized, data)) {
            return -EIO;
        }
        return 0;
    }

    std::string real = to_backing_path(cfg, path);
    if (::truncate(real.c_str(), size) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi) {
    (void)fi;
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    if (logical_file_exists(cfg, normalized)) {
        std::string meta_path = meta_path_for_fuse_path(cfg, normalized);
        std::string data_path = data_path_for_fuse_path(cfg, normalized);
        if (utimensat(AT_FDCWD, meta_path.c_str(), tv, 0) == -1) {
            return -errno;
        }
        if (utimensat(AT_FDCWD, data_path.c_str(), tv, 0) == -1) {
            return -errno;
        }
        return 0;
    }
    std::string real = to_backing_path(cfg, path);
    if (utimensat(AT_FDCWD, real.c_str(), tv, AT_SYMLINK_NOFOLLOW) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_open(const char* path, struct fuse_file_info* fi) {
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    if (!logical_file_exists(cfg, normalized)) {
        std::string real = to_backing_path(cfg, path);
        if (fs::is_directory(real)) {
            return -EISDIR;
        }
        return -ENOENT;
    }

    auto* handle = new OpenFileHandle();
    handle->path = normalized;
    if (!load_logical_file(cfg, normalized, &handle->data)) {
        delete handle;
        return -EIO;
    }
    if ((fi->flags & O_TRUNC) != 0) {
        handle->data.clear();
        handle->dirty = true;
    }
    fi->fh = reinterpret_cast<uint64_t>(handle);
    return 0;
}

static int cfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void)mode;
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);

    if ((fi->flags & O_EXCL) != 0 && logical_file_exists(cfg, normalized)) {
        return -EEXIST;
    }

    auto* handle = new OpenFileHandle();
    handle->path = normalized;
    if (logical_file_exists(cfg, normalized)) {
        if (!load_logical_file(cfg, normalized, &handle->data)) {
            delete handle;
            return -EIO;
        }
        if ((fi->flags & O_TRUNC) != 0) {
            handle->data.clear();
            handle->dirty = true;
        }
    } else {
        handle->dirty = true;
    }

    fi->fh = reinterpret_cast<uint64_t>(handle);
    return 0;
}

static int cfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void)path;
    if (offset < 0) {
        return -EINVAL;
    }
    auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
    if (h == nullptr) {
        return -EBADF;
    }
    size_t off = static_cast<size_t>(offset);
    if (off >= h->data.size()) {
        return 0;
    }
    size_t to_copy = std::min(size, h->data.size() - off);
    memcpy(buf, h->data.data() + off, to_copy);
    return static_cast<int>(to_copy);
}

static int cfs_write(const char* path, const char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi) {
    (void)path;
    if (offset < 0) {
        return -EINVAL;
    }
    auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
    if (h == nullptr) {
        return -EBADF;
    }
    size_t off = static_cast<size_t>(offset);
    size_t need = off + size;
    if (h->data.size() < need) {
        h->data.resize(need, 0);
    }
    memcpy(h->data.data() + off, buf, size);
    h->dirty = true;
    return static_cast<int>(size);
}

static int cfs_statfs(const char* path, struct statvfs* stbuf) {
    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    if (statvfs(real.c_str(), stbuf) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_flush(const char* path, struct fuse_file_info* fi) {
    (void)path;
    auto* cfg = get_cfg();
    auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
    if (h == nullptr) {
        return -EBADF;
    }
    return flush_handle_if_dirty(cfg, h);
}

static int cfs_release(const char* path, struct fuse_file_info* fi) {
    (void)path;
    auto* cfg = get_cfg();
    auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
    if (h == nullptr) {
        return -EBADF;
    }
    int rc = flush_handle_if_dirty(cfg, h);
    delete h;
    fi->fh = 0;
    return rc;
}

static int cfs_fsync(const char* path, int datasync, struct fuse_file_info* fi) {
    (void)path;
    (void)datasync;
    auto* cfg = get_cfg();
    auto* h = reinterpret_cast<OpenFileHandle*>(fi->fh);
    if (h == nullptr) {
        return -EBADF;
    }
    return flush_handle_if_dirty(cfg, h);
}

static bool path_is_direct_child(const std::string& parent, const std::string& child,
                                 std::string* out_name, bool* out_is_dir) {
    std::string p = normalize_path(parent.c_str());
    std::string c = normalize_path(child.c_str());
    if (p == c) {
        return false;
    }
    std::string prefix = p == "/" ? "/" : p + "/";
    if (c.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string remain = c.substr(prefix.size());
    size_t slash = remain.find('/');
    if (slash == std::string::npos) {
        *out_name = remain;
        *out_is_dir = false;
    } else {
        *out_name = remain.substr(0, slash);
        *out_is_dir = true;
    }
    return !out_name->empty();
}

static int cfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                       struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    auto* cfg = get_cfg();
    std::string real = to_backing_path(cfg, path);
    std::string normalized = normalize_path(path);

    if (!fs::is_directory(real)) {
        return -ENOENT;
    }

    if (filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        return 0;
    }
    if (filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        return 0;
    }

    std::set<std::string> added;
    DIR* dir = opendir(real.c_str());
    if (dir == nullptr) {
        return -errno;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0 ||
            strcmp(de->d_name, ".crunchfs") == 0) {
            continue;
        }
        added.insert(de->d_name);
        if (filler(buf, de->d_name, NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);

    std::set<std::string> entries;
    if (!load_index(cfg, &entries)) {
        return -EIO;
    }
    for (const std::string& logical_path : entries) {
        std::string name;
        bool is_dir = false;
        if (!path_is_direct_child(normalized, logical_path, &name, &is_dir)) {
            continue;
        }
        if (added.insert(name).second) {
            struct stat st {};
            st.st_mode = is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
            if (filler(buf, name.c_str(), &st, 0, FUSE_FILL_DIR_PLUS) != 0) {
                return 0;
            }
        }
    }
    return 0;
}

static int cfs_access(const char* path, int mask) {
    auto* cfg = get_cfg();
    std::string normalized = normalize_path(path);
    if (normalized == "/") {
        return 0;
    }
    if (logical_file_exists(cfg, normalized)) {
        if (mask & W_OK) {
            return 0;
        }
        if (mask & R_OK) {
            return 0;
        }
        return 0;
    }
    std::string real = to_backing_path(cfg, path);
    if (faccessat(AT_FDCWD, real.c_str(), mask, AT_SYMLINK_NOFOLLOW) == -1) {
        return -errno;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <backing_store_path> <mount_point> [FUSE options]\n", argv[0]);
        return 1;
    }

    char resolved[PATH_MAX];
    if (realpath(argv[1], resolved) == nullptr) {
        perror("realpath(backing_store_path)");
        return 1;
    }

    cfs_config cfg;
    cfg.backing_dir = resolved;
    if (!ensure_storage_layout(&cfg)) {
        fprintf(stderr, "Failed to initialize .crunchfs backing layout under %s\n", cfg.backing_dir.c_str());
        return 1;
    }

    int fuse_argc = argc - 1;
    char** fuse_argv = new char*[fuse_argc];
    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; ++i) {
        fuse_argv[i - 1] = argv[i];
    }

    static struct fuse_operations cfs_ops = {};
    cfs_ops.getattr = cfs_getattr;
    cfs_ops.readlink = cfs_readlink;
    cfs_ops.mknod = cfs_mknod;
    cfs_ops.mkdir = cfs_mkdir;
    cfs_ops.unlink = cfs_unlink;
    cfs_ops.rmdir = cfs_rmdir;
    cfs_ops.symlink = cfs_symlink;
    cfs_ops.rename = cfs_rename;
    cfs_ops.link = cfs_link;
    cfs_ops.chmod = cfs_chmod;
    cfs_ops.chown = cfs_chown;
    cfs_ops.truncate = cfs_truncate;
    cfs_ops.open = cfs_open;
    cfs_ops.create = cfs_create;
    cfs_ops.read = cfs_read;
    cfs_ops.write = cfs_write;
    cfs_ops.statfs = cfs_statfs;
    cfs_ops.flush = cfs_flush;
    cfs_ops.release = cfs_release;
    cfs_ops.fsync = cfs_fsync;
    cfs_ops.readdir = cfs_readdir;
    cfs_ops.access = cfs_access;
    cfs_ops.utimens = cfs_utimens;

    int ret = fuse_main(fuse_argc, fuse_argv, &cfs_ops, &cfg);
    delete[] fuse_argv;
    return ret;
}
