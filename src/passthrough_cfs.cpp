#define FUSE_USE_VERSION 36
// Must match the libfuse3 headers on your system (see fuse3/fuse_common.h).

#include <fuse3/fuse.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Runtime config; pointer passed to fuse_main as private_data and read via fuse_get_context().
struct cfs_config {
    std::string backing_dir;
};

// Map a FUSE path to the corresponding path under the backing directory.
// Example: "/notes/a.txt" -> "<backing_dir>/notes/a.txt"
static std::string to_backing_path(const cfs_config* cfg, const char* fuse_path) {
    if (strcmp(fuse_path, "/") == 0) {
        return cfg->backing_dir;
    }
    return cfg->backing_dir + fuse_path;
}

static int cfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void)fi;

    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    memset(stbuf, 0, sizeof(struct stat));
    if (lstat(real.c_str(), stbuf) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                       struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    DIR* dir = opendir(real.c_str());
    if (dir == nullptr) {
        return -errno;
    }

    if (filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        closedir(dir);
        return 0;
    }
    if (filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        closedir(dir);
        return 0;
    }

    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        std::string entry_path = real + "/" + de->d_name;
        struct stat st {};
        if (lstat(entry_path.c_str(), &st) == -1) {
            // Broken symlink or race: still list the name with best-effort type from dirent.
            memset(&st, 0, sizeof(st));
            st.st_ino = de->d_ino;
            if (de->d_type != DT_UNKNOWN) {
                // d_type encodes S_IFDIR / S_IFREG / ... in the high bits (see man readdir).
                st.st_mode = static_cast<mode_t>(de->d_type) << 12;
            }
        }

        if (filler(buf, de->d_name, &st, 0, FUSE_FILL_DIR_PLUS) != 0) {
            break;
        }
    }

    closedir(dir);
    return 0;
}

static int cfs_open(const char* path, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    // Read-only passthrough: reject write/create/truncate open modes.
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }

    int fd = ::open(real.c_str(), fi->flags);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = static_cast<uint64_t>(fd);
    return 0;
}

static int cfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    ssize_t bytes_read = pread(fd, buf, size, offset);
    if (bytes_read == -1) {
        return -errno;
    }
    return static_cast<int>(bytes_read);
}

static int cfs_release(const char* path, struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    if (::close(fd) == -1) {
        return -errno;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // Usage: passthrough_cfs <backing_dir> <mountpoint> [FUSE options e.g. -f -s]
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

    // fuse_main expects argv: program name, mountpoint, then optional FUSE flags — not backing_dir.
    int fuse_argc = argc - 1;
    char** fuse_argv = new char*[fuse_argc];
    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; ++i) {
        fuse_argv[i - 1] = argv[i];
    }

    static struct fuse_operations cfs_ops = {};
    cfs_ops.getattr = cfs_getattr;
    cfs_ops.open = cfs_open;
    cfs_ops.read = cfs_read;
    cfs_ops.readdir = cfs_readdir;
    cfs_ops.release = cfs_release;

    int ret = fuse_main(fuse_argc, fuse_argv, &cfs_ops, &cfg);
    delete[] fuse_argv;
    return ret;
}
