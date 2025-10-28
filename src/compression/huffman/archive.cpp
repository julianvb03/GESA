#include "compression/huffman/archive.hpp"

#include "compression/huffman/types.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace gesa::compression::huffman {
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

void writeFrequencies(std::ostream& output, const FrequencyTable& frequencies)
{
    for (auto frequency : frequencies) {
        writeValue(output, frequency);
    }
}

void readFrequencies(std::istream& input, FrequencyTable& frequencies)
{
    for (auto& frequency : frequencies) {
        frequency = readValue<std::uint32_t>(input);
    }
}

} // namespace

ParsedFileHeader readFileHeader(std::istream& input)
{
    char magic[4] = {0, 0, 0, 0};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        throw std::runtime_error("Failed to read file magic");
    }
    if (std::memcmp(magic, kFileMagic, sizeof(magic)) != 0) {
        throw std::runtime_error("Invalid Huffman file magic");
    }

    const auto version = readValue<std::uint8_t>(input);
    if (version != kFormatVersion) {
        throw std::runtime_error("Unsupported Huffman file version");
    }

    char padding[3];
    input.read(padding, sizeof(padding));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(padding))) {
        throw std::runtime_error("Failed to read file padding");
    }

    ParsedFileHeader header {};
    header.metadata.originalSize = readValue<std::uint64_t>(input);
    header.compressedSize = readValue<std::uint64_t>(input);
    readFrequencies(input, header.metadata.frequencies);
    return header;
}

void writeFileHeader(std::ostream& output, const HuffmanMetadata& metadata, std::uint64_t compressedSize)
{
    output.write(kFileMagic, sizeof(kFileMagic));
    if (!output) {
        throw std::runtime_error("Failed to write file magic");
    }

    writeValue(output, kFormatVersion);
    const std::uint8_t padding[3] = {0, 0, 0};
    output.write(reinterpret_cast<const char*>(padding), sizeof(padding));
    if (!output) {
        throw std::runtime_error("Failed to write file padding");
    }

    writeValue(output, metadata.originalSize);
    writeValue(output, compressedSize);
    writeFrequencies(output, metadata.frequencies);
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

    writeValue(output, entry.result.metadata.originalSize);
    const auto compressedSize = static_cast<std::uint64_t>(entry.result.compressed.size());
    writeValue(output, compressedSize);
    writeFrequencies(output, entry.result.metadata.frequencies);
    if (compressedSize > 0U) {
        output.write(reinterpret_cast<const char*>(entry.result.compressed.data()), static_cast<std::streamsize>(entry.result.compressed.size()));
        if (!output) {
            throw std::runtime_error("Failed to write archive payload");
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
        const auto compressedSize = readValue<std::uint64_t>(input);
        readFrequencies(input, entry.metadata.frequencies);

        entry.compressed.resize(static_cast<std::size_t>(compressedSize));
        if (compressedSize > 0U) {
            input.read(reinterpret_cast<char*>(entry.compressed.data()), static_cast<std::streamsize>(compressedSize));
            if (input.gcount() != static_cast<std::streamsize>(compressedSize)) {
                throw std::runtime_error("Failed to read archive compressed payload");
            }
        }

        entries.emplace_back(std::move(entry));
    }

    return entries;
}

std::string readMagic(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file to inspect magic: " + path.string());
    }

    char magic[4] = {0, 0, 0, 0};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        throw std::runtime_error("Unable to read magic header from: " + path.string());
    }

    return std::string(magic, sizeof(magic));
}

} // namespace gesa::compression::huffman
