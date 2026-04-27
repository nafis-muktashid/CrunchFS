#define FUSE_USE_VERSION 36
#include <fuse3/fuse.h>
#include <string>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <shared_mutex>
#include <map>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <ctime>
#include <thread>
#include "fuse_ops.hpp"
#include "metadata.hpp"
#include "chunk_store.hpp"
#include "thread_pool.hpp"

CfsConfig g_cfg{};
static MetaStore*  g_meta  = nullptr;
static ChunkStore* g_store = nullptr;
static ThreadPool* g_pool  = nullptr;

static std::map<std::string, std::shared_mutex*> g_file_locks;
static std::mutex g_locks_mu;

static std::shared_mutex& file_lock(const std::string& id) {
    std::unique_lock<std::mutex> lk(g_locks_mu);
    if (!g_file_locks.count(id))
        g_file_locks[id] = new std::shared_mutex();
    return *g_file_locks[id];
}

static constexpr const char* STATS_VPATH = "/.cfs_stats";

static std::string norm(const char* p) {
    std::string s = p ? p : "/";
    return s.empty() ? "/" : s;
}

static std::string stats_content() {
    size_t l = 0, c = 0;
    g_meta->accumulate_stats(l, c);
    double r = c > 0 ? (double)l / c : 0.0;
    std::ostringstream ss;
    ss << "logical_bytes " << l << "\ncompressed_bytes " << c << "\nratio " << r << "\n";
    return ss.str();
}

static int cfs_getattr(const char* path, struct stat* st, struct fuse_file_info*) {
    memset(st, 0, sizeof(*st));
    std::string p = norm(path);
    if (p == "/") { st->st_mode = S_IFDIR|0755; st->st_nlink = 2; return 0; }
    if (p == STATS_VPATH) {
        std::string sc = stats_content();
        st->st_mode = S_IFREG|0444; st->st_nlink = 1;
        st->st_size = (off_t)sc.length(); return 0;
    }
    std::string id = g_meta->path_to_id(p);
    if (id.empty()) return -ENOENT;
    FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
    st->st_mode = S_IFREG|m.mode; st->st_nlink = 1;
    st->st_size = (off_t)m.logical_size;
    st->st_atime = m.atime; st->st_mtime = m.mtime; st->st_ctime = m.ctime;
    return 0;
}

static int cfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler,
                       off_t, struct fuse_file_info*, enum fuse_readdir_flags) {
    if (norm(path) != "/") return -ENOENT;
    filler(buf, ".", nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, "..", nullptr, 0, FUSE_FILL_DIR_PLUS);
    filler(buf, ".cfs_stats", nullptr, 0, FUSE_FILL_DIR_PLUS);
    for (auto& lp : g_meta->list_paths()) {
        std::string name = (lp.length() > 1 && lp[0] == '/') ? lp.substr(1) : lp;
        filler(buf, name.c_str(), nullptr, 0, FUSE_FILL_DIR_PLUS);
    }
    return 0;
}

static int cfs_open(const char* path, struct fuse_file_info*) {
    std::string p = norm(path);
    if (p == STATS_VPATH) return 0;
    return g_meta->path_to_id(p).empty() ? -ENOENT : 0;
}

static int cfs_create(const char* path, mode_t mode, struct fuse_file_info*) {
    std::string p = norm(path);
    if (!g_meta->path_to_id(p).empty()) return -EEXIST;
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : p) { h ^= c; h *= 1099511628211ULL; }
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << h;
    std::string id = ss.str();
    time_t now = time(nullptr);
    FileMeta m;
    m.path_id = id; m.logical_path = p; m.logical_size = 0;
    m.compressed_size = 0; m.chunk_size = ChunkStore::CHUNK_SIZE;
    m.n_chunks = 0; m.atime = m.mtime = m.ctime = now; m.mode = mode & 0777;
    g_meta->save(m); g_meta->index_add(p, id);
    return 0;
}

static int cfs_read(const char* path, char* buf, size_t size, off_t offset,
                    struct fuse_file_info*) {
    std::string p = norm(path);
    if (p == STATS_VPATH) {
        std::string sc = stats_content(); size_t len = sc.length();
        if ((size_t)offset >= len) return 0;
        size_t n = std::min(size, len - (size_t)offset);
        memcpy(buf, sc.data() + offset, n); return (int)n;
    }
    std::string id = g_meta->path_to_id(p);
    if (id.empty()) return -ENOENT;
    FileMeta m;
    { std::shared_lock<std::shared_mutex> lk(file_lock(id));
      if (!g_meta->load(id, m)) return -ENOENT; }
    if ((size_t)offset >= m.logical_size) return 0;
    size = std::min(size, m.logical_size - (size_t)offset);
    size_t fc = (size_t)offset / ChunkStore::CHUNK_SIZE;
    size_t lc = ((size_t)offset + size - 1) / ChunkStore::CHUNK_SIZE;
    size_t nc = lc - fc + 1;
    std::vector<std::vector<char>> cdata(nc);
    std::atomic<bool> err{false};
    for (size_t i = 0; i < nc; ++i) {
        g_pool->enqueue([&, i]{
            if (err) return;
            std::shared_lock<std::shared_mutex> lk(file_lock(id));
            if (g_store->chunk_disk_size(id, fc + i) == 0) {
                cdata[i].assign(ChunkStore::CHUNK_SIZE, '\0');
                return;
            }
            if (!g_store->read_chunk(id, fc + i, cdata[i])) err = true;
        });
    }
    g_pool->wait_all();
    if (err) return -EIO;
    size_t written = 0;
    for (size_t i = 0; i < nc; ++i) {
        size_t cs = (fc + i) * ChunkStore::CHUNK_SIZE;
        size_t off = (i == 0) ? (size_t)offset - cs : 0;
        auto& cd = cdata[i];
        if (off >= cd.size()) break;
        size_t take = std::min(cd.size() - off, size - written);
        memcpy(buf + written, cd.data() + off, take);
        written += take;
    }
    return (int)written;
}

static int cfs_write(const char* path, const char* buf, size_t size, off_t offset,
                     struct fuse_file_info*) {
    if (size == 0) return 0;
    std::string p = norm(path);
    std::string id = g_meta->path_to_id(p);
    if (id.empty()) return -ENOENT;
    std::unique_lock<std::shared_mutex> lk(file_lock(id));
    FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
    size_t fc = (size_t)offset / ChunkStore::CHUNK_SIZE;
    size_t lc = ((size_t)offset + size - 1) / ChunkStore::CHUNK_SIZE;
    for (size_t ci = fc; ci <= lc; ++ci) {
        size_t cs = ci * ChunkStore::CHUNK_SIZE;
        std::vector<char> existing(ChunkStore::CHUNK_SIZE, '\0');
        size_t el = 0;
        if (ci < m.n_chunks) {
            std::vector<char> tmp;
            el = g_store->read_chunk(id, ci, tmp);
            if (el) { el = std::min(el, ChunkStore::CHUNK_SIZE); memcpy(existing.data(), tmp.data(), el); }
        }
        size_t ws = (ci == fc) ? (size_t)offset - cs : 0;
        size_t bo = (ci == fc) ? 0 : (ci - fc) * ChunkStore::CHUNK_SIZE - ((size_t)offset % ChunkStore::CHUNK_SIZE);
        size_t we = std::min(ChunkStore::CHUNK_SIZE, (size_t)offset + size - cs);
        size_t tc = we - ws; if (bo + tc > size) tc = size - bo;
        memcpy(existing.data() + ws, buf + bo, tc);
        g_store->write_chunk(id, ci, existing.data(), std::max(el, we));
    }
    m.n_chunks = std::max(m.n_chunks, lc + 1);
    m.logical_size = std::max(m.logical_size, (size_t)offset + size);
    m.compressed_size = 0;
    for (size_t i = 0; i < m.n_chunks; ++i) m.compressed_size += g_store->chunk_disk_size(id, i);
    m.mtime = m.ctime = time(nullptr); g_meta->save(m);
    return (int)size;
}

static int cfs_truncate(const char* path, off_t ns, struct fuse_file_info*) {
    std::string p = norm(path);
    std::string id = g_meta->path_to_id(p); if (id.empty()) return -ENOENT;
    std::unique_lock<std::shared_mutex> lk(file_lock(id));
    FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
    size_t nsz = (size_t)ns;
    if (nsz == 0) { g_store->remove_chunks(id, m.n_chunks); m.n_chunks=0; m.logical_size=0; m.compressed_size=0; }
    else if (nsz < m.logical_size) {
        size_t keep = (nsz + ChunkStore::CHUNK_SIZE - 1) / ChunkStore::CHUNK_SIZE;
        for (size_t i = keep; i < m.n_chunks; ++i) g_store->remove_chunk(id, i);
        m.n_chunks = keep; m.logical_size = nsz; m.compressed_size = 0;
        for (size_t i = 0; i < m.n_chunks; ++i) m.compressed_size += g_store->chunk_disk_size(id, i);
    } else { m.logical_size = nsz; }
    m.mtime = m.ctime = time(nullptr); g_meta->save(m); return 0;
}

static int cfs_unlink(const char* path) {
    std::string p = norm(path);
    std::string id = g_meta->path_to_id(p); if (id.empty()) return -ENOENT;
    { std::unique_lock<std::shared_mutex> lk(file_lock(id));
      FileMeta m; if (g_meta->load(id, m)) g_store->remove_chunks(id, m.n_chunks);
      g_meta->remove(id); }
    g_meta->index_remove(p);
    { std::unique_lock<std::mutex> lk(g_locks_mu);
      auto it = g_file_locks.find(id);
      if (it != g_file_locks.end()) { delete it->second; g_file_locks.erase(it); } }
    return 0;
}

static int cfs_rename(const char* from, const char* to, unsigned int) {
    std::string fp = norm(from), tp = norm(to);
    std::string oid = g_meta->path_to_id(fp); if (oid.empty()) return -ENOENT;
    std::string eid = g_meta->path_to_id(tp);
    if (!eid.empty()) {
        FileMeta em; g_meta->load(eid, em);
        g_store->remove_chunks(eid, em.n_chunks);
        g_meta->remove(eid); g_meta->index_remove(tp);
    }
    FileMeta m; g_meta->load(oid, m);
    m.logical_path = tp; g_meta->save(m);
    g_meta->index_remove(fp); g_meta->index_add(tp, oid);
    return 0;
}

static int cfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info*) {
    std::string p = norm(path);
    std::string id = g_meta->path_to_id(p); if (id.empty()) return -ENOENT;
    std::unique_lock<std::shared_mutex> lk(file_lock(id));
    FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
    m.atime = tv[0].tv_sec; m.mtime = tv[1].tv_sec; g_meta->save(m); return 0;
}

static int cfs_chmod(const char* path, mode_t mode, struct fuse_file_info*) {
    std::string p = norm(path);
    std::string id = g_meta->path_to_id(p); if (id.empty()) return -ENOENT;
    FileMeta m; if (!g_meta->load(id, m)) return -ENOENT;
    m.mode = mode & 0777; g_meta->save(m); return 0;
}

static fuse_operations cfs_ops{};

const fuse_operations* get_cfs_operations() {
    cfs_ops.getattr  = cfs_getattr;
    cfs_ops.readdir  = cfs_readdir;
    cfs_ops.open     = cfs_open;
    cfs_ops.create   = cfs_create;
    cfs_ops.read     = cfs_read;
    cfs_ops.write    = cfs_write;
    cfs_ops.truncate = cfs_truncate;
    cfs_ops.unlink   = cfs_unlink;
    cfs_ops.rename   = cfs_rename;
    cfs_ops.utimens  = cfs_utimens;
    cfs_ops.chmod    = cfs_chmod;
    cfs_ops.init = [](struct fuse_conn_info*, struct fuse_config* cfg) -> void* {
        cfg->kernel_cache = 0;
        std::string bd = g_cfg.backing_dir;
        g_meta  = new MetaStore(bd);
        g_store = new ChunkStore(bd);
        size_t workers = std::max<size_t>(1, std::thread::hardware_concurrency());
        g_pool  = new ThreadPool(workers);
        return nullptr;
    };
    cfs_ops.destroy = [](void*) {
        delete g_pool; g_pool = nullptr;
        delete g_store; g_store = nullptr;
        delete g_meta; g_meta = nullptr;
    };
    return &cfs_ops;
}
