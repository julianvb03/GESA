#include "compression/huffman/bit_stream.hpp"

#include <utility>

namespace gesa::compression::huffman {

void BitWriter::writeBit(bool bit)
{
    current_ = static_cast<std::uint8_t>((current_ << 1U) | static_cast<std::uint8_t>(bit));
    ++bitCount_;
    if (bitCount_ == 8U) {
        buffer_.push_back(current_);
        current_ = 0;
        bitCount_ = 0;
    }
}

void BitWriter::writeCode(const std::vector<bool>& bits)
{
    for (bool bit : bits) {
        writeBit(bit);
    }
}

std::vector<std::uint8_t> BitWriter::finish()
{
    if (bitCount_ > 0U) {
        current_ <<= (8U - bitCount_);
        buffer_.push_back(current_);
        current_ = 0;
        bitCount_ = 0;
    }
    return std::move(buffer_);
}

BitReader::BitReader(const std::uint8_t* data, std::size_t size)
    : data_(data)
    , size_(size)
{
}

bool BitReader::readBit(bool& bit)
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

} // namespace gesa::compression::huffman
