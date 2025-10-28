#include "compression/lzw/codec.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gesa::compression::lzw {

CompressionResult encodeBuffer(const std::vector<std::uint8_t>& input)
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

std::vector<std::uint8_t> decodeBuffer(const LZWMetadata& metadata, const std::vector<std::uint16_t>& codes)
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

    const auto resolve = [&](std::uint16_t code) -> std::string {
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

} // namespace gesa::compression::lzw
