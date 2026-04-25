#include "thread_pool.hpp"

ThreadPool::ThreadPool(size_t n) {
    workers_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lk(mu_);
                    cv_.wait(lk, [this]{ return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                    ++active_;
                }
                task();
                {
                    std::unique_lock lk(mu_);
                    --active_;
                    if (tasks_.empty() && active_ == 0) cv_done_.notify_all();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    { std::unique_lock lk(mu_); stop_ = true; }
    cv_.notify_all();
    for (auto& w : workers_) w.join();
}

void ThreadPool::enqueue(std::function<void()> task) {
    { std::unique_lock lk(mu_); tasks_.push(std::move(task)); }
    cv_.notify_one();
}

void ThreadPool::wait_all() {
    std::unique_lock lk(mu_);
    cv_done_.wait(lk, [this]{ return tasks_.empty() && active_ == 0; });
}
