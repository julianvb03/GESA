#include "compression/lzw.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

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

TEST(LZWCompressionTest, CompressAndDecompressFile)
{
    ScopedTempDir temp("lzw_file");
    const auto source = temp.path() / "data.txt";
    const auto compressed = temp.path() / "data.lzw";
    const auto restored = temp.path() / "restored.txt";

    const std::string payload = "Sphinx of black quartz, judge my vow.\n";
    writeBinaryFile(source, payload);

    gesa::compression::lzw::compressFile(source, compressed);
    gesa::compression::lzw::decompressFile(compressed, restored);

    EXPECT_EQ(readBinaryFile(source), readBinaryFile(restored));
}

TEST(LZWCompressionTest, CompressAndDecompressDirectory)
{
    ScopedTempDir temp("lzw_dir");
    const auto inputDir = temp.path() / "input";
    const auto outputDir = temp.path() / "output";
    const auto archive = temp.path() / "archive.glza";

    std::filesystem::create_directories(inputDir / "nested");
    writeBinaryFile(inputDir / "root.txt", "Root level contents");
    writeBinaryFile(inputDir / "nested" / "alpha.bin", std::string(256, '\x01'));
    writeBinaryFile(inputDir / "nested" / "beta.bin", "beta payload\nwith multiple lines\n");

    gesa::compression::lzw::compressDirectory(inputDir, archive, 2);
    gesa::compression::lzw::decompressDirectory(archive, outputDir, 2);

    const auto originalFiles = collectFiles(inputDir);
    const auto restoredFiles = collectFiles(outputDir);
    EXPECT_EQ(originalFiles, restoredFiles);

    for (const auto& relative : originalFiles) {
        const auto source = inputDir / relative;
        const auto restored = outputDir / relative;
        EXPECT_EQ(readBinaryFile(source), readBinaryFile(restored));
    }
}
