#pragma once

#include "concurrency/thread_pool.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>

namespace gesa::filesystem {

enum class EntryType {
    File,
    Directory
};

struct FileDescriptor {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    EntryType type {EntryType::File};
    std::uintmax_t size {0};
    std::filesystem::file_time_type lastWriteTime {};
    bool isSymlink {false};
};

FileDescriptor describePath(const std::filesystem::path& path);

class FileContext {
public:
    explicit FileContext(std::filesystem::path sourcePath);

    const FileDescriptor& descriptor() const noexcept;

    std::vector<std::uint8_t> readAll() const;
    std::vector<std::uint8_t> readRange(std::uintmax_t offset, std::size_t length) const;
    void writeAll(const std::filesystem::path& destinationPath, const std::vector<std::uint8_t>& data) const;
    void copyTo(const std::filesystem::path& destinationPath) const;

private:
    FileDescriptor descriptor_;
};

class DirectoryContext {
public:
    DirectoryContext(std::filesystem::path rootPath, bool followSymlinks = false);

    const std::filesystem::path& root() const noexcept;
    bool followsSymlinks() const noexcept;

    std::vector<FileDescriptor> listEntries(bool recursive = true, bool includeDirectories = true) const;

    template <class Callable>
    void forEachFile(Callable&& callback, bool recursive = true, std::size_t threadCount = 0) const;

private:
    FileDescriptor buildDescriptor(const std::filesystem::directory_entry& entry) const;

    std::filesystem::path rootPath_;
    bool followSymlinks_ {false};
};

// Template definitions

template <class Callable>
void DirectoryContext::forEachFile(Callable&& callback, bool recursive, std::size_t threadCount) const
{
    const auto entries = listEntries(recursive, false);
    if (entries.empty()) {
        return;
    }

    gesa::concurrency::ThreadPool pool(threadCount);
    auto sharedCallback = std::make_shared<std::decay_t<Callable>>(std::forward<Callable>(callback));

    std::vector<std::future<void>> futures;
    futures.reserve(entries.size());

    for (const auto& entry : entries) {
        futures.emplace_back(pool.enqueue([sharedCallback, entry]() { (*sharedCallback)(entry); }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace gesa::filesystem
