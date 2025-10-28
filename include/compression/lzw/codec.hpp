#pragma once

#include "compression/lzw/types.hpp"

#include <cstdint>
#include <vector>

namespace gesa::compression::lzw {

CompressionResult encodeBuffer(const std::vector<std::uint8_t>& input);
std::vector<std::uint8_t> decodeBuffer(const LZWMetadata& metadata, const std::vector<std::uint16_t>& codes);

} // namespace gesa::compression::lzw
