#pragma once

struct fuse_operations;
const fuse_operations* get_cfs_operations();

struct CfsConfig {
    const char* backing_dir;
};
extern CfsConfig g_cfg;