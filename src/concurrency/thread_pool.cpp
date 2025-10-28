#include "concurrency/thread_pool.hpp"

#include <algorithm>
#include <stdexcept>

namespace gesa::concurrency {

ThreadPool::ThreadPool(std::size_t threadCount)
{
    if (threadCount == 0) {
        threadCount = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }

    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }

    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t ThreadPool::size() const noexcept
{
    return workers_.size();
}

void ThreadPool::workerLoop()
{
    while (true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();
    }
}

} // namespace gesa::concurrency
