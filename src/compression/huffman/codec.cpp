#include "compression/huffman/codec.hpp"

#include "compression/huffman/bit_stream.hpp"

#include <array>
#include <memory>
#include <queue>
#include <stdexcept>
#include <utility>

namespace gesa::compression::huffman {
namespace {

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

struct CodeTableEntry {
    std::vector<bool> bits;
};

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

} // namespace

CompressionResult encodeBuffer(const std::vector<std::uint8_t>& input)
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

std::vector<std::uint8_t> decodeBuffer(const HuffmanMetadata& metadata, const std::vector<std::uint8_t>& compressed)
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

} // namespace gesa::compression::huffman
