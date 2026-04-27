#pragma once
#include <string>
#include <mutex>
#include <ctime>
#include <vector>
#include <unordered_map>

struct FileMeta {
    std::string path_id;
    std::string logical_path;
    size_t      logical_size{0};
    size_t      compressed_size{0};
    size_t      chunk_size{0};
    size_t      n_chunks{0};
    time_t      atime{0}, mtime{0}, ctime{0};
    mode_t      mode{0644};
};

class MetaStore {
public:
    explicit MetaStore(const std::string& backing_dir);
    std::string path_to_id(const std::string& logical_path);
    bool load(const std::string& path_id, FileMeta& out);
    bool save(const FileMeta& m);
    bool remove(const std::string& path_id);
    std::vector<std::string> list_paths();
    void index_add(const std::string& logical_path, const std::string& path_id);
    void index_remove(const std::string& logical_path);
    void accumulate_stats(size_t& total_logical, size_t& total_compressed);

private:
    std::string meta_dir_;
    std::string index_file_;
    std::mutex  mu_;
    std::unordered_map<std::string,std::string> index_;
    void load_index();
    void save_index();
    std::string meta_path(const std::string& path_id) const;
    std::string hash_path(const std::string& logical_path);
};