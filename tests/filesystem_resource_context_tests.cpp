#include "filesystem/resource_context.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class ScopedTempDir {
public:
    explicit ScopedTempDir(const std::string& prefix)
    {
        const auto unique = prefix + "_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() / unique;
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::set<std::string> toRelativeSet(const std::vector<gesa::filesystem::FileDescriptor>& entries)
{
    std::set<std::string> names;
    for (const auto& entry : entries) {
        names.insert(entry.relativePath.generic_string());
    }
    return names;
}

} // namespace

TEST(FileContextTest, ReadWriteAndCopy)
{
    ScopedTempDir temp("file_context");
    const auto source = temp.path() / "original.bin";

    const std::string payload = "abcdefghijklmnopqrstuvwxyz";
    writeFile(source, payload);

    EXPECT_THROW(gesa::filesystem::FileContext(temp.path()), std::invalid_argument);

    gesa::filesystem::FileContext file(source);

    const auto allData = file.readAll();
    ASSERT_EQ(allData.size(), payload.size());
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), allData.begin()));

    const auto rangeData = file.readRange(5, 4);
    ASSERT_EQ(rangeData.size(), 4U);
    EXPECT_EQ(std::string(rangeData.begin(), rangeData.end()), payload.substr(5, 4));

    const auto dest = temp.path() / "nested" / "copy.bin";
    file.writeAll(dest, allData);
    std::ifstream written(dest, std::ios::binary);
    std::string writtenContent((std::istreambuf_iterator<char>(written)), std::istreambuf_iterator<char>());
    EXPECT_EQ(writtenContent, payload);

    const auto copied = temp.path() / "duplicated.bin";
    file.copyTo(copied);
    std::ifstream copiedStream(copied, std::ios::binary);
    std::string copiedContent((std::istreambuf_iterator<char>(copiedStream)), std::istreambuf_iterator<char>());
    EXPECT_EQ(copiedContent, payload);
}

TEST(DirectoryContextTest, ListsEntries)
{
    ScopedTempDir temp("dir_context");
    const auto root = temp.path();
    const auto subdir = root / "sub";
    std::filesystem::create_directories(subdir);

    writeFile(root / "a.txt", "alpha");
    writeFile(subdir / "b.txt", "beta");
    writeFile(subdir / "c.txt", "gamma");

    gesa::filesystem::DirectoryContext directory(root);

    const auto entriesFlat = directory.listEntries(false, true);
    const auto flatNames = toRelativeSet(entriesFlat);
    EXPECT_EQ(flatNames, (std::set<std::string>{"a.txt", "sub"}));

    const auto entriesFlatFilesOnly = directory.listEntries(false, false);
    const auto flatFilesNames = toRelativeSet(entriesFlatFilesOnly);
    EXPECT_EQ(flatFilesNames, (std::set<std::string>{"a.txt"}));

    const auto entriesRecursive = directory.listEntries(true, false);
    const auto recursiveNames = toRelativeSet(entriesRecursive);
    EXPECT_EQ(recursiveNames, (std::set<std::string>{"a.txt", "sub/b.txt", "sub/c.txt"}));
}

TEST(DirectoryContextTest, ForEachFileRunsInParallel)
{
    ScopedTempDir temp("dir_context_parallel");
    const auto root = temp.path();
    const auto subdir = root / "sub";
    std::filesystem::create_directories(subdir);

    writeFile(root / "a.txt", "alpha");
    writeFile(subdir / "b.txt", "beta");
    writeFile(subdir / "c.txt", "gamma");

    gesa::filesystem::DirectoryContext directory(root);

    std::mutex mutex;
    std::vector<std::string> visited;

    directory.forEachFile(
        [&](const gesa::filesystem::FileDescriptor& descriptor) {
            std::lock_guard<std::mutex> lock(mutex);
            visited.push_back(descriptor.relativePath.generic_string());
        },
        true,
        2);

    std::sort(visited.begin(), visited.end());
    EXPECT_EQ(visited, (std::vector<std::string>{"a.txt", "sub/b.txt", "sub/c.txt"}));
}
