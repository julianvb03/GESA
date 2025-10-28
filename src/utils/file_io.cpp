#include "utils/file_io.hpp"

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace gesa::utils {

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

} // namespace gesa::utils
