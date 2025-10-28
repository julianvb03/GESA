#include "compression/huffman.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
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

void writeBinaryFile(const std::filesystem::path& path, const std::string& content)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << content;
}

std::string readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::set<std::filesystem::path> collectFiles(const std::filesystem::path& root)
{
    std::set<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.insert(std::filesystem::relative(entry.path(), root));
        }
    }
    return files;
}

} // namespace

TEST(HuffmanCompressionTest, CompressAndDecompressFile)
{
    ScopedTempDir temp("huffman_file");
    const auto source = temp.path() / "data.txt";
    const auto compressed = temp.path() / "data.huf";
    const auto restored = temp.path() / "restored.txt";

    const std::string payload = "The quick brown fox jumps over the lazy dog.\n";
    writeBinaryFile(source, payload);

    gesa::compression::huffman::compressFile(source, compressed);
    gesa::compression::huffman::decompressFile(compressed, restored);

    EXPECT_EQ(readBinaryFile(source), readBinaryFile(restored));
}

TEST(HuffmanCompressionTest, CompressAndDecompressDirectory)
{
    ScopedTempDir temp("huffman_dir");
    const auto inputDir = temp.path() / "input";
    const auto outputDir = temp.path() / "output";
    const auto archive = temp.path() / "archive.ghar";

    std::filesystem::create_directories(inputDir / "nested");
    writeBinaryFile(inputDir / "root.txt", "root file contents");
    writeBinaryFile(inputDir / "nested" / "alpha.bin", std::string(512, 'A'));
    writeBinaryFile(inputDir / "nested" / "beta.bin", std::string("beta payload"));

    gesa::compression::huffman::compressDirectory(inputDir, archive, 2);
    gesa::compression::huffman::decompressDirectory(archive, outputDir, 2);

    const auto originalFiles = collectFiles(inputDir);
    const auto restoredFiles = collectFiles(outputDir);
    EXPECT_EQ(originalFiles, restoredFiles);

    for (const auto& relative : originalFiles) {
        const auto source = inputDir / relative;
        const auto restored = outputDir / relative;
        EXPECT_EQ(readBinaryFile(source), readBinaryFile(restored));
    }
}
