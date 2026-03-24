// hello_fs.cpp
// Minimal "Hello, World!" FUSE filesystem (read-only).
// Compile with: g++ -o hello_fs hello_fs.cpp -lfuse3
// Run with: ./hello_fs /mnt/hello
// Then: cat /mnt/hello/hello.txt should print "Hello, World!"


// FUSE_USE_VERSION is required for libfuse3 compatibility.
#define FUSE_USE_VERSION 36

#include <fuse3/fuse.h> 

#include <iostream>     // for cout
#include <string>       // for string
#include <cstring>      // for memset, strlen, memcpy

// The only visible file is /hello.txt, which contains "Hello, World!"
static const char* hello_path = "/hello.txt";
static const char* hello_str = "Hello, World!\n";

// FUSE callback: called by the kernel to get the attributes of a file or directory. (like ls -l or stat)
static int cfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void)fi; // unused parameter

    //clear the output buffer
    memset(stbuf, 0, sizeof(struct stat));
 
    //root directory: reporting as a directory 
    if(strcmp(path, "/") == 0){
        stbuf->st_mode = S_IFDIR | 0755;   //directory +  permissions
        stbuf->st_nlink = 2;               //typically has 2 links: . and ..
        return 0;
    } else if(strcmp(path, hello_path) == 0){  //regular file
        stbuf->st_mode = S_IFREG | 0644;       //regular file + permissions
        stbuf->st_nlink = 1;                   //typically has 1 link: itself
        stbuf->st_size = strlen(hello_str);    //size of the file
        return 0;
    }
    return -ENOENT;                            //file not found
}

// FUSE callback: called by the kernel to list the contents of a directory. (like ls)
static int cfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void) offset;     //unused parameter
    (void) fi;         //unused parameter
    (void) flags;      //unused parameter

    if(strcmp(path, "/") == 0)
        return -ENOENT; //not a directory

    filler(buf, ".", NULL, 0, FUSE_FILL_DIR_PLUS);  //current directory
    filler(buf, "..", NULL, 0, FUSE_FILL_DIR_PLUS); //parent directory
    filler(buf, hello_path, NULL, 0, FUSE_FILL_DIR_PLUS); //hello.txt
    return 0;                                        //success
}

// FUSE callback: called by the kernel to open a file. (like cat)
static int cfs_open(const char* path, struct fuse_file_info* fi) {
    //only allow reading the hello.txt file
    if(strcmp(path, hello_path) != 0){
        return -ENOENT; //file not found
    }
    if((fi->flags & O_ACCMODE) != O_RDONLY){
        return -EACCES; //not allowed to open for writing
    }
    return 0; //success
}

// FUSE callback: called by the kernel to read a file. (like cat)
static int cfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void) fi;

    if(strcmp(path, hello_path) != 0){
        return -ENOENT;
    }

    size_t len = strlen(hello_str);  //length of the file
    if(offset >= (off_t)len){
        return 0; //end of file
    }
    if(offset + size > len){
        size = len - offset; //limit the read to the end of the file
    }
    memcpy(buf, hello_str + offset, size);
    return (int)size; //return the number of bytes read
}

// FUSE operations: a table of callbacks for the kernel to use
static const struct fuse_operations cfs_ops = {
    .getattr = cfs_getattr,
    .open = cfs_open,
    .read = cfs_read,
    .readdir = cfs_readdir,
};


int main(int argc, char* argv[]) {
    return fuse_main(argc, argv, &cfs_ops, nullptr); //start the FUSE filesystem
}