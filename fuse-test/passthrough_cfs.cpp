#define FUSE_USE_VERSION 36

#include <fuse3/fuse.h>
#include <iostream>
#include <string.h>
#include <string>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//Holds runtime configuration passed from main()
struct cfs_config {
    std::string backing_dir;
};

//Build absolute path in backing store from FUSE path.
//Example: "/notes/a.txt" -> "/backing_dir/notes/a.txt"
static std::string to_backing_path(const cfs_config* cfg, const char* fuse_path) {
    if(strcmp(fuse_path, "/") == 0) {
        return cfg->backing_dir;
    }

    //fuse_path begins with "/", so concatenate with backing_dir.
    return cfg->backing_dir + fuse_path;
}

static int cfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void) fi;

    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    memset(stbuf, 0, sizeof(struct stat));
    if(lstat(real.c_str(), stbuf) == -1) { //-1 means error
        return -errno;
    }

    return 0;
}

static int cfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void) offset;
    (void) fi;
    (void) flags;

    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    DIR* dir = opendir(real.c_str());
    if(dir == nullptr) {
        return -errno;
    }

    //Include . and .. in the directory listing
    if(filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        closedir(dir);
        return 0;
    }
    if(filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS) != 0) {
        closedir(dir);
        return 0;
    }

    //Read directory entries
    struct dirent* de;
    while((de = readdir(dir)) != nullptr) {
        //Skip "." and ".."
        if(strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        //Get the full path to the entry
        std::string entry_path = real + "/" + de->d_name;
        struct stat st;
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;  //DT_* -> S_IFMT (file type) | permissions (0644) 
        
        if(filler(buf, de->d_name, &st, 0, FUSE_FILL_DIR_PLUS) != 0) {
            break;
        }
    }

    closedir(dir);
    return 0;
}


static int cfs_open(const char* path, struct fuse_file_info* fi) {
    auto* cfg = static_cast<const cfs_config*>(fuse_get_context()->private_data);
    std::string real = to_backing_path(cfg, path);

    //Check if the file exists and is a regular file read-only
    if((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }

    int fd = ::open(real.c_str(), fi->flags);
    if(fd == -1) {
        return -errno;
    }

    fi->fh = static_cast<uint64_t>(fd);
    return 0;
}


static int cfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) path;

    int fd = static_cast<int>(fi->fh);   //cast to int because fi->fh is uint64_t
    ssize_t bytes_read = pread(fd, buf, size, offset);  //pread is a safe way to read a file
    if(bytes_read == -1) {   //-1 means error
        return -errno;  
    }
    return static_cast<int>(bytes_read);
}


static int cfs_release(const char* path, struct fuse_file_info* fi) {
    (void) path;

    int fd = static_cast<int>(fi->fh);  
    if(::close(fd) == -1) {
        return -errno;
    }
    return 0;
}


int main(int argc, char* argv[]) {
    //Expecting: ./passthrough_cfs <backing_store_path> <mount_point>
    if(argc < 3) {
        fprintf(stderr, "Usage: %s <backing_store_path> <mount_point> [mount_options]\n", argv[0]);
        return 1;
    }

    char resolved[PATH_MAX];
    if(realpath(argv[1], resolved) == nullptr) {
        perror("realpath(backing_store_path)");
        return 1;
    }

    cfs_config cfg;
    cfg.backing_dir = resolved;

    //Parse mount options
    int fuse_argc = argc - 1;
    char** fuse_argv = new char*[fuse_argc];
    fuse_argv[0] = argv[0];
    for(int i = 2; i < argc; ++i) {
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