#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gesa::compression::lzw {

inline constexpr char kFileMagic[4] = {'G', 'L', 'Z', 'W'};
inline constexpr char kArchiveMagic[4] = {'G', 'L', 'Z', 'A'};
inline constexpr std::uint8_t kFormatVersion = 1;
inline constexpr std::uint16_t kInitialDictionarySize = 256;
inline constexpr std::uint16_t kMaxDictionarySize = 4096;

struct LZWMetadata {
    std::uint64_t originalSize {0};
    std::uint16_t dictionarySize {0};
};

struct CompressionResult {
    LZWMetadata metadata;
    std::vector<std::uint16_t> codes;
};

struct ParsedFileHeader {
    LZWMetadata metadata;
    std::uint64_t codeCount {0};
};

struct ArchiveEntry {
    std::filesystem::path relativePath;
    LZWMetadata metadata;
    std::vector<std::uint16_t> codes;
};

struct PendingArchiveEntry {
    std::filesystem::path relativePath;
    LZWMetadata metadata;
    std::vector<std::uint16_t> codes;
};

} // namespace gesa::compression::lzw
