#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gesa::utils {

void ensureParentDirectory(const std::filesystem::path& path);
void writeBufferToFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& data);

} // namespace gesa::utils
