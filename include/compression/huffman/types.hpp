#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace gesa::compression::huffman {

inline constexpr char kFileMagic[4] = {'G', 'H', 'U', 'F'};
inline constexpr char kArchiveMagic[4] = {'G', 'H', 'A', 'R'};
inline constexpr std::uint8_t kFormatVersion = 1;

using FrequencyTable = std::array<std::uint32_t, 256>;

struct HuffmanMetadata {
    FrequencyTable frequencies {};
    std::uint64_t originalSize {0};
};

struct CompressionResult {
    HuffmanMetadata metadata;
    std::vector<std::uint8_t> compressed;
};

struct ParsedFileHeader {
    HuffmanMetadata metadata;
    std::uint64_t compressedSize {0};
};

struct ArchiveEntry {
    std::filesystem::path relativePath;
    CompressionResult result;
};

struct PendingArchiveEntry {
    std::filesystem::path relativePath;
    HuffmanMetadata metadata;
    std::vector<std::uint8_t> compressed;
};

} // namespace gesa::compression::huffman
