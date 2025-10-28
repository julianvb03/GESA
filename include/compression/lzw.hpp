#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace gesa::compression::lzw {

void compressFile(const std::filesystem::path& source, const std::filesystem::path& destination);
void decompressFile(const std::filesystem::path& source, const std::filesystem::path& destination);

void compressDirectory(const std::filesystem::path& sourceDirectory,
                       const std::filesystem::path& destinationArchive,
                       std::size_t threadCount = 0);

void decompressDirectory(const std::filesystem::path& sourceArchive,
                         const std::filesystem::path& destinationDirectory,
                         std::size_t threadCount = 0);

} // namespace gesa::compression::lzw
