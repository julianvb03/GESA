#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <stdexcept>

namespace gesa::concurrency {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <class Callable, class... Args>
    auto enqueue(Callable&& task, Args&&... args)
        -> std::future<std::invoke_result_t<Callable, Args...>>;

    std::size_t size() const noexcept;

private:
    using Task = std::function<void()>;

    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ {false};
};


template <class Callable, class... Args>
auto ThreadPool::enqueue(Callable&& task, Args&&... args) -> std::future<std::invoke_result_t<Callable, Args...>>
{
    using Result = std::invoke_result_t<Callable, Args...>;

    auto packagedTask = std::make_shared<std::packaged_task<Result()>>(
        std::bind(std::forward<Callable>(task), std::forward<Args>(args)...));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        tasks_.emplace([packagedTask]() { (*packagedTask)(); });
    }

    cv_.notify_one();
    return packagedTask->get_future();
}

} // namespace gesa::concurrency
