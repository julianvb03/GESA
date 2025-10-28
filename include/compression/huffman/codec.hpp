#pragma once

#include "compression/huffman/types.hpp"

#include <cstdint>
#include <vector>

namespace gesa::compression::huffman {

CompressionResult encodeBuffer(const std::vector<std::uint8_t>& input);
std::vector<std::uint8_t> decodeBuffer(const HuffmanMetadata& metadata, const std::vector<std::uint8_t>& compressed);

} // namespace gesa::compression::huffman
