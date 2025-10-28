#pragma once

#include "compression/huffman/types.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace gesa::compression::huffman {

ParsedFileHeader readFileHeader(std::istream& input);
void writeFileHeader(std::ostream& output, const HuffmanMetadata& metadata, std::uint64_t compressedSize);

void writeArchiveHeader(std::ostream& output, std::uint32_t fileCount);
void writeArchiveEntry(std::ostream& output, const ArchiveEntry& entry);
std::vector<PendingArchiveEntry> readArchive(std::istream& input);

std::string readMagic(const std::filesystem::path& path);

} // namespace gesa::compression::huffman
