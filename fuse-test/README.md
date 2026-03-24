# fuse-test (optional demos)

Source-only examples. **Do not commit compiled binaries** (`hello_fs`, `passthrough_cfs`); build into `build/` or ignore locally.

## hello_fs (minimal FUSE “hello world”)

```bash
g++ -std=c++17 hello_fs.cpp -lfuse3 -o hello_fs
mkdir -p /tmp/hello_mount
./hello_fs /tmp/hello_mount -f
# other terminal: ls /tmp/hello_mount && cat /tmp/hello_mount/hello.txt
```

Passthrough lives under **`src/passthrough_cfs.cpp`**; build with CMake from the repo root (see main README).
