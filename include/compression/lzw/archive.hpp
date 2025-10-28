#pragma once

#include "compression/lzw/types.hpp"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace gesa::compression::lzw {

ParsedFileHeader readFileHeader(std::istream& input);
void writeFileHeader(std::ostream& output, const LZWMetadata& metadata, std::uint64_t codeCount);

void writeArchiveHeader(std::ostream& output, std::uint32_t fileCount);
void writeArchiveEntry(std::ostream& output, const ArchiveEntry& entry);
std::vector<PendingArchiveEntry> readArchive(std::istream& input);

} // namespace gesa::compression::lzw
