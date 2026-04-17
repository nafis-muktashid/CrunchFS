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
#include <sys/statvfs.h>
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

static int cfs_readlink(const char* path, char* buf, size_t size) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    ssize_t len = readlink(real.c_str(), buf, size - 1);
    if (len == -1) {
        return -errno;
    }
    buf[len] = '\0';
    return 0;
}

static int cfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
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
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    if (::mkdir(real.c_str(), mode) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_unlink(const char* path) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    if (::unlink(real.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_rmdir(const char* path) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    if (::rmdir(real.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_symlink(const char* target, const char* linkpath) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real_link = to_backing_path(cfg, linkpath);

    if (::symlink(target, real_link.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_rename(const char* from, const char* to, unsigned int flags) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real_from = to_backing_path(cfg, from);
    std::string real_to = to_backing_path(cfg, to);

    if (renameat2(AT_FDCWD, real_from.c_str(), AT_FDCWD, real_to.c_str(), flags) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_link(const char* from, const char* to) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real_from = to_backing_path(cfg, from);
    std::string real_to = to_backing_path(cfg, to);

    if (::link(real_from.c_str(), real_to.c_str()) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_chmod(const char* path, mode_t mode, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    int ret;
    if (fi != nullptr && fi->fh != 0) {
        ret = fchmod(static_cast<int>(fi->fh), mode);
    } else {
        ret = chmod(real.c_str(), mode);
    }
    if (ret == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_chown(const char* path, uid_t uid, gid_t gid, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    int ret;
    if (fi != nullptr && fi->fh != 0) {
        ret = fchown(static_cast<int>(fi->fh), uid, gid);
    } else {
        ret = lchown(real.c_str(), uid, gid);
    }
    if (ret == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    int ret;
    if (fi != nullptr && fi->fh != 0) {
        ret = ftruncate(static_cast<int>(fi->fh), size);
    } else {
        ret = truncate(real.c_str(), size);
    }
    if (ret == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_utimens(const char* path, const struct timespec tv[2], struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    int ret;
    if (fi != nullptr && fi->fh != 0) {
        ret = futimens(static_cast<int>(fi->fh), tv);
    } else {
        ret = utimensat(AT_FDCWD, real.c_str(), tv, AT_SYMLINK_NOFOLLOW);
    }
    if (ret == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_open(const char* path, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    // O_CREAT / O_EXCL are handled by create() when implemented; open() sees a clean open.
    int fd = ::open(real.c_str(), fi->flags);
    if (fd == -1) {
        return -errno;
    }

    fi->fh = static_cast<uint64_t>(fd);
    return 0;
}

static int cfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    int fd = ::open(real.c_str(), fi->flags, mode);
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

static int cfs_write(const char* path, const char* buf, size_t size, off_t offset,
                     struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    ssize_t written = pwrite(fd, buf, size, offset);
    if (written == -1) {
        return -errno;
    }
    return static_cast<int>(written);
}

static int cfs_statfs(const char* path, struct statvfs* stbuf) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    if (statvfs(real.c_str(), stbuf) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_flush(const char* path, struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    int copy = dup(fd);
    if (copy == -1) {
        return -errno;
    }
    if (::close(copy) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_release(const char* path, struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    if (::close(fd) == -1) {
        return -errno;
    }
    return 0;
}

static int cfs_fsync(const char* path, int datasync, struct fuse_file_info* fi) {
    (void)path;

    int fd = static_cast<int>(fi->fh);
    int ret = datasync != 0 ? fdatasync(fd) : fsync(fd);
    if (ret == -1) {
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

static int cfs_access(const char* path, int mask) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    if (faccessat(AT_FDCWD, real.c_str(), mask, AT_SYMLINK_NOFOLLOW) == -1) {
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
