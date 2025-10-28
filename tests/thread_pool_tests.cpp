#include "concurrency/thread_pool.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using gesa::concurrency::ThreadPool;

TEST(ThreadPoolTest, ExecutesMultipleTasks)
{
    ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(pool.enqueue([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return i * i;
        }));
    }

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(futures[i].get(), i * i);
    }
}

TEST(ThreadPoolTest, PropagatesExceptions)
{
    ThreadPool pool(2);

    auto future = pool.enqueue([]() -> void {
        throw std::runtime_error("boom");
    });

    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(ThreadPoolTest, HonorsThreadCount)
{
    ThreadPool pool(3);
    EXPECT_EQ(pool.size(), static_cast<std::size_t>(3));
}

} // namespace
