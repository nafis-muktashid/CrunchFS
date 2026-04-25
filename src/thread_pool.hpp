#pragma once
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

class ThreadPool {
public:
    explicit ThreadPool(size_t n_threads);
    ~ThreadPool();

    // Submit a task; returns immediately.
    void enqueue(std::function<void()> task);

    // Wait until all submitted tasks are finished.
    void wait_all();

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::condition_variable           cv_done_;
    std::atomic<size_t>               active_{0};
    bool                              stop_{false};
};
