#include "compression/huffman.hpp"

#include "compression/huffman/archive.hpp"
#include "compression/huffman/codec.hpp"
#include "compression/huffman/types.hpp"
#include "concurrency/thread_pool.hpp"
#include "filesystem/resource_context.hpp"
#include "utils/file_io.hpp"

#include <fstream>
#include <future>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace {

gesa::compression::huffman::ArchiveEntry compressEntry(const gesa::filesystem::FileDescriptor& descriptor)
{
    const auto result = gesa::compression::huffman::encodeBuffer(gesa::filesystem::FileContext(descriptor.absolutePath).readAll());
    return gesa::compression::huffman::ArchiveEntry {descriptor.relativePath, result};
}

std::vector<std::uint8_t> readFilePayload(std::istream& input, std::uint64_t size)
{
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (size > 0U) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) {
            throw std::runtime_error("Failed to read compressed payload");
        }
    }
    return buffer;
}

} // namespace

namespace gesa::compression::huffman {

void compressFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    gesa::filesystem::FileContext context(source);
    const auto buffer = context.readAll();
    const auto result = encodeBuffer(buffer);

    gesa::utils::ensureParentDirectory(destination);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open destination for writing: " + destination.string());
    }

    writeFileHeader(output, result.metadata, static_cast<std::uint64_t>(result.compressed.size()));
    if (!result.compressed.empty()) {
        output.write(reinterpret_cast<const char*>(result.compressed.data()), static_cast<std::streamsize>(result.compressed.size()));
        if (!output) {
            throw std::runtime_error("Failed to write compressed payload");
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
    const auto compressed = readFilePayload(input, header.compressedSize);
    const auto decompressed = decodeBuffer(header.metadata, compressed);
    gesa::utils::writeBufferToFile(destination, decompressed);
}

void compressDirectory(const std::filesystem::path& sourceDirectory,
                       const std::filesystem::path& destinationArchive,
                       std::size_t threadCount)
{
    gesa::filesystem::DirectoryContext directory(sourceDirectory);
    const auto descriptors = directory.listEntries(true, false);

    std::vector<ArchiveEntry> entries;
    entries.reserve(descriptors.size());

    if (!descriptors.empty()) {
        gesa::concurrency::ThreadPool pool(threadCount);
        std::vector<std::future<ArchiveEntry>> futures;
        futures.reserve(descriptors.size());

        for (const auto& descriptor : descriptors) {
            futures.emplace_back(pool.enqueue([descriptor]() { return compressEntry(descriptor); }));
        }

        for (auto& future : futures) {
            entries.emplace_back(future.get());
        }
    }

    gesa::utils::ensureParentDirectory(destinationArchive);
    std::ofstream output(destinationArchive, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open archive for writing: " + destinationArchive.string());
    }

    writeArchiveHeader(output, static_cast<std::uint32_t>(entries.size()));
    for (const auto& entry : entries) {
        writeArchiveEntry(output, entry);
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

    auto entries = readArchive(input);
    if (entries.empty()) {
        return;
    }

    gesa::concurrency::ThreadPool pool(threadCount);
    std::vector<std::future<void>> futures;
    futures.reserve(entries.size());

    for (auto& entry : entries) {
        const auto outputPath = destinationDirectory / entry.relativePath;
        futures.emplace_back(pool.enqueue([outputPath, metadata = entry.metadata, compressed = std::move(entry.compressed)]() mutable {
            const auto decompressed = decodeBuffer(metadata, compressed);
            gesa::utils::writeBufferToFile(outputPath, decompressed);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace gesa::compression::huffman
