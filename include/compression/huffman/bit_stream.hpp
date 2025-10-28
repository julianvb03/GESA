#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gesa::compression::huffman {

class BitWriter {
public:
    BitWriter() = default;

    void writeBit(bool bit);
    void writeCode(const std::vector<bool>& bits);
    std::vector<std::uint8_t> finish();

private:
    std::vector<std::uint8_t> buffer_;
    std::uint8_t current_ {0};
    std::uint8_t bitCount_ {0};
};

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size);

    bool readBit(bool& bit);

private:
    const std::uint8_t* data_ {nullptr};
    std::size_t size_ {0};
    std::size_t byteIndex_ {0};
    std::uint8_t bitIndex_ {0};
};

} // namespace gesa::compression::huffman
