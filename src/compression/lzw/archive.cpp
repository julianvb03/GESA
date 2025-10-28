#include "compression/lzw/archive.hpp"

#include "compression/lzw/types.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gesa::compression::lzw {
namespace {

template <class T>
void writeValue(std::ostream& output, T value)
{
    output.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!output) {
        throw std::runtime_error("Failed to write binary value");
    }
}

template <class T>
T readValue(std::istream& input)
{
    T value {};
    input.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(T))) {
        throw std::runtime_error("Failed to read binary value");
    }
    return value;
}

} // namespace

ParsedFileHeader readFileHeader(std::istream& input)
{
    char magic[4] = {0, 0, 0, 0};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        throw std::runtime_error("Failed to read LZW file magic");
    }

    if (std::memcmp(magic, kFileMagic, sizeof(magic)) != 0) {
        throw std::runtime_error("Invalid LZW file magic");
    }

    const auto version = readValue<std::uint8_t>(input);
    if (version != kFormatVersion) {
        throw std::runtime_error("Unsupported LZW file version");
    }

    char padding[3];
    input.read(padding, sizeof(padding));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(padding))) {
        throw std::runtime_error("Failed to read LZW file padding");
    }

    ParsedFileHeader header {};
    header.metadata.originalSize = readValue<std::uint64_t>(input);
    header.metadata.dictionarySize = readValue<std::uint16_t>(input);
    header.codeCount = readValue<std::uint64_t>(input);
    return header;
}

void writeFileHeader(std::ostream& output, const LZWMetadata& metadata, std::uint64_t codeCount)
{
    output.write(kFileMagic, sizeof(kFileMagic));
    if (!output) {
        throw std::runtime_error("Failed to write LZW file magic");
    }

    writeValue(output, kFormatVersion);
    const std::uint8_t padding[3] = {0, 0, 0};
    output.write(reinterpret_cast<const char*>(padding), sizeof(padding));
    if (!output) {
        throw std::runtime_error("Failed to write LZW file padding");
    }

    writeValue(output, metadata.originalSize);
    writeValue(output, metadata.dictionarySize);
    writeValue(output, codeCount);
}

void writeArchiveHeader(std::ostream& output, std::uint32_t fileCount)
{
    output.write(kArchiveMagic, sizeof(kArchiveMagic));
    if (!output) {
        throw std::runtime_error("Failed to write archive magic");
    }

    writeValue(output, kFormatVersion);
    const std::uint8_t padding[3] = {0, 0, 0};
    output.write(reinterpret_cast<const char*>(padding), sizeof(padding));
    if (!output) {
        throw std::runtime_error("Failed to write archive padding");
    }

    writeValue(output, fileCount);
}

void writeArchiveEntry(std::ostream& output, const ArchiveEntry& entry)
{
    const auto relative = entry.relativePath.generic_string();
    if (relative.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("Relative path exceeds maximum supported length");
    }

    const auto pathSize = static_cast<std::uint32_t>(relative.size());
    writeValue(output, pathSize);
    if (pathSize > 0U) {
        output.write(relative.data(), static_cast<std::streamsize>(relative.size()));
        if (!output) {
            throw std::runtime_error("Failed to write archive path");
        }
    }

    writeValue(output, entry.metadata.originalSize);
    writeValue(output, entry.metadata.dictionarySize);

    const auto codeCount = static_cast<std::uint64_t>(entry.codes.size());
    writeValue(output, codeCount);
    if (codeCount > 0U) {
        output.write(reinterpret_cast<const char*>(entry.codes.data()), static_cast<std::streamsize>(entry.codes.size() * sizeof(std::uint16_t)));
        if (!output) {
            throw std::runtime_error("Failed to write archive code stream");
        }
    }
}

std::vector<PendingArchiveEntry> readArchive(std::istream& input)
{
    char magic[4] = {0, 0, 0, 0};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        throw std::runtime_error("Failed to read archive magic");
    }

    if (std::memcmp(magic, kArchiveMagic, sizeof(magic)) != 0) {
        throw std::runtime_error("Invalid archive magic");
    }

    const auto version = readValue<std::uint8_t>(input);
    if (version != kFormatVersion) {
        throw std::runtime_error("Unsupported archive version");
    }

    char padding[3];
    input.read(padding, sizeof(padding));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(padding))) {
        throw std::runtime_error("Failed to read archive padding");
    }

    const auto fileCount = readValue<std::uint32_t>(input);

    std::vector<PendingArchiveEntry> entries;
    entries.reserve(fileCount);

    for (std::uint32_t index = 0; index < fileCount; ++index) {
        const auto pathSize = readValue<std::uint32_t>(input);
        std::string relativePath(pathSize, '\0');
        if (pathSize > 0U) {
            input.read(relativePath.data(), static_cast<std::streamsize>(pathSize));
            if (input.gcount() != static_cast<std::streamsize>(pathSize)) {
                throw std::runtime_error("Failed to read archive path");
            }
        }

        PendingArchiveEntry entry {};
        entry.relativePath = std::filesystem::path(relativePath);
        entry.metadata.originalSize = readValue<std::uint64_t>(input);
        entry.metadata.dictionarySize = readValue<std::uint16_t>(input);
        const auto codeCount = readValue<std::uint64_t>(input);

        entry.codes.resize(static_cast<std::size_t>(codeCount));
        if (codeCount > 0U) {
            input.read(reinterpret_cast<char*>(entry.codes.data()), static_cast<std::streamsize>(codeCount * sizeof(std::uint16_t)));
            if (input.gcount() != static_cast<std::streamsize>(codeCount * sizeof(std::uint16_t))) {
                throw std::runtime_error("Failed to read archive code stream");
            }
        }

        entries.emplace_back(std::move(entry));
    }

    return entries;
}

} // namespace gesa::compression::lzw
