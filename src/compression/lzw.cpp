#include "compression/lzw.hpp"

#include "concurrency/thread_pool.hpp"
#include "filesystem/resource_context.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gesa::compression::lzw {
namespace {

constexpr char kFileMagic[4] = {'G', 'L', 'Z', 'W'};
constexpr char kArchiveMagic[4] = {'G', 'L', 'Z', 'A'};
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::uint16_t kInitialDictionarySize = 256;
constexpr std::uint16_t kMaxDictionarySize = 4096; // 12-bit codes

struct LZWMetadata {
    std::uint64_t originalSize {0};
    std::uint16_t dictionarySize {0};
};

struct CompressionResult {
    LZWMetadata metadata;
    std::vector<std::uint16_t> codes;
};

void ensureParentDirectory(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        throw std::filesystem::filesystem_error("create_directories", parent, ec);
    }
}

void writeBufferToFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data)
{
    ensureParentDirectory(path);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }

    if (!data.empty()) {
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!output) {
            throw std::runtime_error("Failed to write file contents: " + path.string());
        }
    }
}

CompressionResult encode(const std::vector<std::uint8_t>& input)
{
    CompressionResult result {};
    result.metadata.originalSize = static_cast<std::uint64_t>(input.size());

    if (input.empty()) {
        return result;
    }

    std::unordered_map<std::string, std::uint16_t> dictionary;
    dictionary.reserve(kMaxDictionarySize);

    for (std::uint16_t code = 0; code < kInitialDictionarySize; ++code) {
        dictionary.emplace(std::string(1, static_cast<char>(code)), code);
    }

    std::uint16_t nextCode = kInitialDictionarySize;
    std::string current;

    for (auto byte : input) {
        const char character = static_cast<char>(byte);
        std::string combined = current;
        combined.push_back(character);

        const auto iterator = dictionary.find(combined);
        if (iterator != dictionary.end()) {
            current = std::move(combined);
        } else {
            if (current.empty()) {
                throw std::runtime_error("LZW encoder encountered empty current sequence");
            }

            result.codes.push_back(dictionary.at(current));
            if (nextCode < kMaxDictionarySize) {
                dictionary.emplace(std::move(combined), nextCode++);
            }

            current.clear();
            current.push_back(character);
        }
    }

    if (!current.empty()) {
        result.codes.push_back(dictionary.at(current));
    }

    result.metadata.dictionarySize = nextCode;
    return result;
}

std::vector<std::uint8_t> decode(const LZWMetadata& metadata, const std::vector<std::uint16_t>& codes)
{
    if (metadata.originalSize == 0U) {
        return {};
    }

    if (codes.empty()) {
        throw std::runtime_error("LZW decoder received empty code stream for non-empty file");
    }

    std::vector<std::string> dictionary;
    dictionary.reserve(kMaxDictionarySize);

    for (std::uint16_t code = 0; code < kInitialDictionarySize; ++code) {
        dictionary.emplace_back(1, static_cast<char>(code));
    }

    std::uint16_t nextCode = kInitialDictionarySize;
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(metadata.originalSize));

    auto extractString = [&](std::uint16_t code) -> std::string {
        if (code < dictionary.size()) {
            return dictionary[code];
        }
        if (code == nextCode && !dictionary.empty()) {
            const auto& previous = dictionary.back();
            return previous + previous.front();
        }
        throw std::runtime_error("Invalid LZW code encountered during decoding");
    };

    const std::uint16_t firstCode = codes.front();
    if (firstCode >= dictionary.size()) {
        throw std::runtime_error("Invalid first LZW code");
    }

    std::string current = dictionary[firstCode];
    output.insert(output.end(), current.begin(), current.end());

    for (std::size_t index = 1; index < codes.size(); ++index) {
        const std::uint16_t code = codes[index];
        std::string entry;

        if (code < dictionary.size()) {
            entry = dictionary[code];
        } else if (code == nextCode) {
            entry = current + current.front();
        } else {
            throw std::runtime_error("Invalid LZW code encountered during decoding");
        }

        output.insert(output.end(), entry.begin(), entry.end());

        if (nextCode < kMaxDictionarySize) {
            dictionary.emplace_back(current + entry.front());
            ++nextCode;
        }

        current = std::move(entry);
    }

    if (output.size() != metadata.originalSize) {
        output.resize(static_cast<std::size_t>(metadata.originalSize));
    }

    return output;
}

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

CompressionResult compressFileInternal(const std::filesystem::path& source)
{
    gesa::filesystem::FileContext context(source);
    const auto data = context.readAll();
    return encode(data);
}

struct ArchiveEntry {
    std::filesystem::path relativePath;
    LZWMetadata metadata;
    std::vector<std::uint16_t> codes;
};

ArchiveEntry compressEntry(const gesa::filesystem::FileDescriptor& descriptor)
{
    auto result = compressFileInternal(descriptor.absolutePath);
    return ArchiveEntry {descriptor.relativePath, result.metadata, std::move(result.codes)};
}

struct ParsedFileHeader {
    LZWMetadata metadata;
    std::uint64_t codeCount {0};
};

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

} // namespace

void compressFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    const auto result = compressFileInternal(source);

    ensureParentDirectory(destination);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open destination for writing: " + destination.string());
    }

    writeFileHeader(output, result.metadata, static_cast<std::uint64_t>(result.codes.size()));
    if (!result.codes.empty()) {
        output.write(reinterpret_cast<const char*>(result.codes.data()),
                     static_cast<std::streamsize>(result.codes.size() * sizeof(std::uint16_t)));
        if (!output) {
            throw std::runtime_error("Failed to write LZW code stream");
        }
    }
}

void decompressFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open compressed file: " + source.string());
    }

    const auto header = readFileHeader(input);
    std::vector<std::uint16_t> codes(static_cast<std::size_t>(header.codeCount));

    if (header.codeCount > 0U) {
        input.read(reinterpret_cast<char*>(codes.data()),
                   static_cast<std::streamsize>(header.codeCount * sizeof(std::uint16_t)));
        if (input.gcount() != static_cast<std::streamsize>(header.codeCount * sizeof(std::uint16_t))) {
            throw std::runtime_error("Failed to read LZW code stream");
        }
    }

    const auto decompressed = decode(header.metadata, codes);
    writeBufferToFile(destination, decompressed);
}

void compressDirectory(const std::filesystem::path& sourceDirectory,
                       const std::filesystem::path& destinationArchive,
                       std::size_t threadCount)
{
    gesa::filesystem::DirectoryContext directory(sourceDirectory);
    const auto entries = directory.listEntries(true, false);

    std::vector<ArchiveEntry> compressedEntries;
    compressedEntries.reserve(entries.size());

    if (!entries.empty()) {
        gesa::concurrency::ThreadPool pool(threadCount);
        std::vector<std::future<ArchiveEntry>> futures;
        futures.reserve(entries.size());

        for (const auto& entry : entries) {
            futures.emplace_back(pool.enqueue([descriptor = entry]() {
                return compressEntry(descriptor);
            }));
        }

        for (auto& future : futures) {
            compressedEntries.emplace_back(future.get());
        }
    }

    ensureParentDirectory(destinationArchive);
    std::ofstream output(destinationArchive, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open archive for writing: " + destinationArchive.string());
    }

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

    writeValue(output, static_cast<std::uint32_t>(compressedEntries.size()));

    for (const auto& entry : compressedEntries) {
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
            output.write(reinterpret_cast<const char*>(entry.codes.data()),
                         static_cast<std::streamsize>(entry.codes.size() * sizeof(std::uint16_t)));
            if (!output) {
                throw std::runtime_error("Failed to write archive code stream");
            }
        }
    }
}

void decompressDirectory(const std::filesystem::path& sourceArchive,
                         const std::filesystem::path& destinationDirectory,
                         std::size_t threadCount)
{
    std::ifstream input(sourceArchive, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open archive: " + sourceArchive.string());
    }

    std::error_code ec;
    std::filesystem::create_directories(destinationDirectory, ec);
    if (ec) {
        throw std::filesystem::filesystem_error("create_directories", destinationDirectory, ec);
    }

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

    struct PendingEntry {
        std::filesystem::path relativePath;
        LZWMetadata metadata;
        std::vector<std::uint16_t> codes;
    };

    std::vector<PendingEntry> entries;
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

        PendingEntry entry {};
        entry.relativePath = std::filesystem::path(relativePath);
        entry.metadata.originalSize = readValue<std::uint64_t>(input);
        entry.metadata.dictionarySize = readValue<std::uint16_t>(input);
        const auto codeCount = readValue<std::uint64_t>(input);

        entry.codes.resize(static_cast<std::size_t>(codeCount));
        if (codeCount > 0U) {
            input.read(reinterpret_cast<char*>(entry.codes.data()),
                       static_cast<std::streamsize>(codeCount * sizeof(std::uint16_t)));
            if (input.gcount() != static_cast<std::streamsize>(codeCount * sizeof(std::uint16_t))) {
                throw std::runtime_error("Failed to read archive code stream");
            }
        }

        entries.emplace_back(std::move(entry));
    }

    if (entries.empty()) {
        return;
    }

    gesa::concurrency::ThreadPool pool(threadCount);
    std::vector<std::future<void>> futures;
    futures.reserve(entries.size());

    for (auto& entry : entries) {
        auto outputPath = destinationDirectory / entry.relativePath;
        auto metadata = entry.metadata;
        auto codes = std::move(entry.codes);
        futures.emplace_back(pool.enqueue([outputPath, metadata, codes = std::move(codes)]() mutable {
            const auto decompressed = decode(metadata, codes);
            writeBufferToFile(outputPath, decompressed);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace gesa::compression::lzw
