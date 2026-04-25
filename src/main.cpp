#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "fuse_ops.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <backing_dir> <mount_point> [fuse_opts...]\n", argv[0]);
        return 1;
    }
    static char backing[4096];
    strncpy(backing, argv[1], sizeof(backing) - 1);
    g_cfg.backing_dir = backing;

    int fuse_argc = argc - 1;
    char** fuse_argv = new char*[fuse_argc];
    fuse_argv[0] = argv[0];
    for (int i = 2; i < argc; ++i) fuse_argv[i - 1] = argv[i];

    fs::create_directories(backing);
    fs::create_directories(std::string(backing) + "/.crunchfs/meta");
    fs::create_directories(std::string(backing) + "/.crunchfs/data");

    int ret = fuse_main(fuse_argc, fuse_argv, get_cfs_operations(), nullptr);
    delete[] fuse_argv;
    return ret;
}