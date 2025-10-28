#include "compression/huffman.hpp"

#include "filesystem/resource_context.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <future>
#include <limits>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gesa::compression::huffman {
namespace {

constexpr char kFileMagic[4] = {'G', 'H', 'U', 'F'};
constexpr char kArchiveMagic[4] = {'G', 'H', 'A', 'R'};
constexpr std::uint8_t kFormatVersion = 1;

struct Node {
    std::uint64_t frequency {0};
    int symbol {-1};
    Node* left {nullptr};
    Node* right {nullptr};
};

struct NodePtrComparator {
    bool operator()(const Node* lhs, const Node* rhs) const noexcept
    {
        if (lhs->frequency == rhs->frequency) {
            return lhs->symbol > rhs->symbol;
        }
        return lhs->frequency > rhs->frequency;
    }
};

struct CodeTableEntry {
    std::vector<bool> bits;
};

using FrequencyTable = std::array<std::uint32_t, 256>;

struct HuffmanMetadata {
    FrequencyTable frequencies {};
    std::uint64_t originalSize {0};
};

struct CompressionResult {
    HuffmanMetadata metadata;
    std::vector<std::uint8_t> compressed;
};

class BitWriter {
public:
    void writeBit(bool bit)
    {
        current_ = static_cast<std::uint8_t>((current_ << 1U) | static_cast<std::uint8_t>(bit));
        ++bitCount_;
        if (bitCount_ == 8U) {
            buffer_.push_back(current_);
            current_ = 0;
            bitCount_ = 0;
        }
    }

    void writeCode(const std::vector<bool>& bits)
    {
        for (bool bit : bits) {
            writeBit(bit);
        }
    }

    std::vector<std::uint8_t> finish()
    {
        if (bitCount_ > 0U) {
            current_ <<= (8U - bitCount_);
            buffer_.push_back(current_);
            current_ = 0;
            bitCount_ = 0;
        }
        return std::move(buffer_);
    }

private:
    std::vector<std::uint8_t> buffer_;
    std::uint8_t current_ {0};
    std::uint8_t bitCount_ {0};
};

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size)
        : data_(data)
        , size_(size)
    {
    }

    bool readBit(bool& bit)
    {
        if (byteIndex_ >= size_) {
            return false;
        }
        const auto current = data_[byteIndex_];
        bit = static_cast<bool>((current >> (7U - bitIndex_)) & 0x1U);
        ++bitIndex_;
        if (bitIndex_ == 8U) {
            bitIndex_ = 0;
            ++byteIndex_;
        }
        return true;
    }

private:
    const std::uint8_t* data_ {nullptr};
    std::size_t size_ {0};
    std::size_t byteIndex_ {0};
    std::uint8_t bitIndex_ {0};
};

using NodeStorage = std::vector<std::unique_ptr<Node>>;

Node* createLeaf(NodeStorage& storage, std::uint64_t frequency, int symbol)
{
    storage.emplace_back(std::make_unique<Node>(Node {frequency, symbol, nullptr, nullptr}));
    return storage.back().get();
}

Node* createInternal(NodeStorage& storage, Node* left, Node* right)
{
    storage.emplace_back(std::make_unique<Node>(Node {left->frequency + right->frequency, -1, left, right}));
    return storage.back().get();
}

Node* buildTree(const FrequencyTable& frequencies, NodeStorage& storage)
{
    std::priority_queue<Node*, std::vector<Node*>, NodePtrComparator> queue;

    for (std::size_t symbol = 0; symbol < frequencies.size(); ++symbol) {
        if (frequencies[symbol] == 0U) {
            continue;
        }
        queue.push(createLeaf(storage, frequencies[symbol], static_cast<int>(symbol)));
    }

    if (queue.empty()) {
        return nullptr;
    }

    while (queue.size() > 1U) {
        Node* left = queue.top();
        queue.pop();
        Node* right = queue.top();
        queue.pop();

        Node* parent = createInternal(storage, left, right);
        queue.push(parent);
    }

    return queue.top();
}

void buildCodeTable(Node* node, std::vector<bool>& prefix, std::array<CodeTableEntry, 256>& table)
{
    if (!node) {
        return;
    }

    if (!node->left && !node->right) {
        auto& entry = table[static_cast<std::size_t>(node->symbol)];
        if (prefix.empty()) {
            entry.bits.push_back(false);
        } else {
            entry.bits = prefix;
        }
        return;
    }

    prefix.push_back(false);
    buildCodeTable(node->left, prefix, table);
    prefix.back() = true;
    buildCodeTable(node->right, prefix, table);
    prefix.pop_back();
}

CompressionResult encode(const std::vector<std::uint8_t>& input)
{
    CompressionResult result {};
    result.metadata.originalSize = static_cast<std::uint64_t>(input.size());

    if (input.empty()) {
        return result;
    }

    FrequencyTable& frequencies = result.metadata.frequencies;
    for (const auto value : input) {
        ++frequencies[static_cast<std::size_t>(value)];
    }

    NodeStorage storage;
    storage.reserve(512);
    Node* root = buildTree(frequencies, storage);
    if (!root) {
        return result;
    }

    std::array<CodeTableEntry, 256> table;
    std::vector<bool> prefix;
    buildCodeTable(root, prefix, table);

    BitWriter writer;
    for (const auto value : input) {
        const auto& bits = table[static_cast<std::size_t>(value)].bits;
        if (bits.empty()) {
            throw std::runtime_error("Invalid Huffman code table entry");
        }
        writer.writeCode(bits);
    }

    result.compressed = writer.finish();
    return result;
}

std::vector<std::uint8_t> decode(const HuffmanMetadata& metadata, const std::vector<std::uint8_t>& compressed)
{
    std::vector<std::uint8_t> output;
    output.reserve(static_cast<std::size_t>(metadata.originalSize));

    if (metadata.originalSize == 0U) {
        return output;
    }

    NodeStorage storage;
    storage.reserve(512);
    Node* root = buildTree(metadata.frequencies, storage);
    if (!root) {
        throw std::runtime_error("Invalid Huffman metadata: empty tree with non-zero size");
    }

    if (!root->left && !root->right) {
        const auto symbol = static_cast<std::uint8_t>(root->symbol);
        output.assign(static_cast<std::size_t>(metadata.originalSize), symbol);
        return output;
    }

    BitReader reader(compressed.data(), compressed.size());
    Node* current = root;
    while (output.size() < metadata.originalSize) {
        bool bit = false;
        if (!reader.readBit(bit)) {
            throw std::runtime_error("Unexpected end of compressed stream");
        }
        current = bit ? current->right : current->left;
        if (!current) {
            throw std::runtime_error("Corrupted Huffman tree traversal");
        }
        if (!current->left && !current->right) {
            output.push_back(static_cast<std::uint8_t>(current->symbol));
            current = root;
        }
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

void writeFrequencies(std::ostream& output, const FrequencyTable& frequencies)
{
    for (auto frequency : frequencies) {
        writeValue(output, frequency);
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

void readFrequencies(std::istream& input, FrequencyTable& frequencies)
{
    for (auto& frequency : frequencies) {
        frequency = readValue<std::uint32_t>(input);
    }
}

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

CompressionResult compressFileInternal(const std::filesystem::path& source)
{
    gesa::filesystem::FileContext context(source);
    const auto buffer = context.readAll();
    return encode(buffer);
}

struct ArchiveEntry {
    std::filesystem::path relativePath;
    CompressionResult result;
};

ArchiveEntry compressEntry(const gesa::filesystem::FileDescriptor& descriptor)
{
    auto result = compressFileInternal(descriptor.absolutePath);
    ArchiveEntry entry {descriptor.relativePath, std::move(result)};
    return entry;
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
        throw std::runtime_error("Failed to write padding");
    }
    writeValue(output, metadata.originalSize);
    writeValue(output, compressedSize);
    writeFrequencies(output, metadata.frequencies);
}

struct ParsedFileHeader {
    HuffmanMetadata metadata;
    std::uint64_t compressedSize {0};
};

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

} // namespace

void compressFile(const std::filesystem::path& source, const std::filesystem::path& destination)
{
    const auto result = compressFileInternal(source);

    ensureParentDirectory(destination);
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
    std::vector<std::uint8_t> compressed(header.compressedSize);
    if (header.compressedSize > 0U) {
        input.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(header.compressedSize));
        if (input.gcount() != static_cast<std::streamsize>(header.compressedSize)) {
            throw std::runtime_error("Failed to read compressed payload");
        }
    }

    const auto decompressed = decode(header.metadata, compressed);
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
        HuffmanMetadata metadata;
        std::vector<std::uint8_t> compressed;
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

        auto& entry = entries.emplace_back();
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
    }

    if (entries.empty()) {
        return;
    }

    gesa::concurrency::ThreadPool pool(threadCount);
    std::vector<std::future<void>> futures;
    futures.reserve(entries.size());

    for (auto& entry : entries) {
        const auto outputPath = destinationDirectory / entry.relativePath;
        futures.emplace_back(pool.enqueue([outputPath, metadata = entry.metadata, compressed = std::move(entry.compressed)]() {
            const auto decompressed = decode(metadata, compressed);
            writeBufferToFile(outputPath, decompressed);
        }));
    }

    for (auto& future : futures) {
        future.get();
    }
}

} // namespace gesa::compression::huffman
