#include "filesystem/resource_context.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace gesa::filesystem {

namespace {

std::filesystem::path makeAbsolute(const std::filesystem::path& path)
{
    std::error_code ec;
    auto absolute = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return absolute;
    }

    absolute = std::filesystem::absolute(path, ec);
    if (!ec) {
        return absolute;
    }

    return path;
}

EntryType resolveType(const std::filesystem::directory_entry& entry)
{
    std::error_code ec;
    if (entry.is_directory(ec)) {
        return EntryType::Directory;
    }

    return EntryType::File;
}

EntryType resolveType(const std::filesystem::path& path)
{
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return EntryType::Directory;
    }

    return EntryType::File;
}

std::filesystem::file_time_type safeLastWriteTime(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::filesystem::file_time_type {};
    }
    return time;
}

std::uintmax_t safeFileSize(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return 0;
    }
    return size;
}

bool safeIsSymlink(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_symlink(path, ec);
}

} // namespace

FileDescriptor describePath(const std::filesystem::path& path)
{
    if (path.empty()) {
        throw std::invalid_argument("Provided path is empty");
    }

    const auto absolute = makeAbsolute(path);

    FileDescriptor descriptor {};
    descriptor.absolutePath = absolute;
    descriptor.relativePath = absolute.filename();
    descriptor.type = resolveType(path);
    descriptor.isSymlink = safeIsSymlink(path);
    descriptor.lastWriteTime = safeLastWriteTime(path);

    if (descriptor.type == EntryType::Directory) {
        descriptor.size = 0;
    } else {
        descriptor.size = safeFileSize(path);
    }

    return descriptor;
}

FileContext::FileContext(std::filesystem::path sourcePath)
    : descriptor_(describePath(std::move(sourcePath)))
{
    if (descriptor_.type != EntryType::File) {
        throw std::invalid_argument("FileContext requires a regular file");
    }
}

const FileDescriptor& FileContext::descriptor() const noexcept
{
    return descriptor_;
}

std::vector<std::uint8_t> FileContext::readAll() const
{
    std::ifstream input(descriptor_.absolutePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file for reading: " + descriptor_.absolutePath.string());
    }

    input.seekg(0, std::ios::end);
    const auto endPosition = input.tellg();
    if (endPosition < 0) {
        throw std::runtime_error("Failed to determine file size: " + descriptor_.absolutePath.string());
    }
    const auto size = static_cast<std::size_t>(endPosition);
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> buffer(size);
    if (!buffer.empty()) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() != static_cast<std::streamsize>(buffer.size())) {
            throw std::runtime_error("Failed to read entire file: " + descriptor_.absolutePath.string());
        }
    }

    return buffer;
}

std::vector<std::uint8_t> FileContext::readRange(std::uintmax_t offset, std::size_t length) const
{
    const auto fileSize = safeFileSize(descriptor_.absolutePath);
    if (offset >= fileSize) {
        return {};
    }

    const auto available = static_cast<std::size_t>(std::min<std::uintmax_t>(fileSize - offset, length));

    std::ifstream input(descriptor_.absolutePath, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file for reading: " + descriptor_.absolutePath.string());
    }

    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) {
        throw std::runtime_error("Failed to seek file: " + descriptor_.absolutePath.string());
    }

    std::vector<std::uint8_t> buffer(available);
    if (!buffer.empty()) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto readBytes = static_cast<std::size_t>(input.gcount());
        buffer.resize(readBytes);
    }

    return buffer;
}

void FileContext::writeAll(const std::filesystem::path& destinationPath, const std::vector<std::uint8_t>& data) const
{
    if (destinationPath.empty()) {
        throw std::invalid_argument("Destination path is empty");
    }

    const auto absoluteDestination = makeAbsolute(destinationPath);
    const auto parent = absoluteDestination.parent_path();

    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::filesystem::filesystem_error("create_directories", parent, ec);
        }
    }

    std::ofstream output(absoluteDestination, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Failed to open file for writing: " + absoluteDestination.string());
    }

    if (!data.empty()) {
        output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!output) {
            throw std::runtime_error("Failed to write data to: " + absoluteDestination.string());
        }
    }
}

void FileContext::copyTo(const std::filesystem::path& destinationPath) const
{
    if (destinationPath.empty()) {
        throw std::invalid_argument("Destination path is empty");
    }

    const auto absoluteDestination = makeAbsolute(destinationPath);
    const auto parent = absoluteDestination.parent_path();

    if (!parent.empty()) {
        std::error_code createError;
        std::filesystem::create_directories(parent, createError);
        if (createError) {
            throw std::filesystem::filesystem_error("create_directories", parent, createError);
        }
    }

    std::error_code copyError;
    std::filesystem::copy_file(
        descriptor_.absolutePath,
        absoluteDestination,
        std::filesystem::copy_options::overwrite_existing,
        copyError);

    if (copyError) {
        throw std::filesystem::filesystem_error("copy_file", descriptor_.absolutePath, absoluteDestination, copyError);
    }
}

DirectoryContext::DirectoryContext(std::filesystem::path rootPath, bool followSymlinks)
    : rootPath_(makeAbsolute(std::move(rootPath)))
    , followSymlinks_(followSymlinks)
{
    std::error_code ec;
    if (!std::filesystem::exists(rootPath_, ec) || !std::filesystem::is_directory(rootPath_, ec)) {
        throw std::invalid_argument("DirectoryContext requires an existing directory");
    }
}

const std::filesystem::path& DirectoryContext::root() const noexcept
{
    return rootPath_;
}

bool DirectoryContext::followsSymlinks() const noexcept
{
    return followSymlinks_;
}

std::vector<FileDescriptor> DirectoryContext::listEntries(bool recursive, bool includeDirectories) const
{
    std::vector<FileDescriptor> entries;

    const auto options = followSymlinks_
        ? std::filesystem::directory_options::follow_directory_symlink
        : std::filesystem::directory_options::none;

    std::error_code ec;

    if (!recursive) {
        std::filesystem::directory_iterator iterator(rootPath_, options, ec);
        if (ec) {
            throw std::filesystem::filesystem_error("directory_iterator", rootPath_, ec);
        }

        for (const auto& entry : iterator) {
            const auto descriptor = buildDescriptor(entry);
            if (!includeDirectories && descriptor.type == EntryType::Directory) {
                continue;
            }
            entries.push_back(descriptor);
        }
    } else {
        std::filesystem::recursive_directory_iterator iterator(rootPath_, options, ec);
        if (ec) {
            throw std::filesystem::filesystem_error("recursive_directory_iterator", rootPath_, ec);
        }

        for (const auto& entry : iterator) {
            const auto descriptor = buildDescriptor(entry);
            if (!includeDirectories && descriptor.type == EntryType::Directory) {
                continue;
            }
            entries.push_back(descriptor);
        }
    }

    return entries;
}

FileDescriptor DirectoryContext::buildDescriptor(const std::filesystem::directory_entry& entry) const
{
    auto descriptor = describePath(entry.path());

    std::error_code ec;
    auto relative = std::filesystem::relative(descriptor.absolutePath, rootPath_, ec);
    if (ec) {
        relative = descriptor.absolutePath.lexically_relative(rootPath_);
    }
    if (relative.empty()) {
        relative = descriptor.absolutePath.filename();
    }
    descriptor.relativePath = std::move(relative);

    descriptor.type = resolveType(entry);
    if (descriptor.type == EntryType::File) {
        descriptor.size = safeFileSize(entry.path());
    }

    descriptor.lastWriteTime = safeLastWriteTime(entry.path());
    descriptor.isSymlink = entry.is_symlink(ec);

    return descriptor;
}

} // namespace gesa::filesystem
