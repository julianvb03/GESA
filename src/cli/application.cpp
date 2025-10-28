#include "cli/application.hpp"

#include "compression/huffman.hpp"
#include "compression/lzw.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

enum class Command {
    Compress,
    Decompress,
    Help
};

enum class Algorithm {
    Huffman,
    LZW
};

struct Options {
    Command command {Command::Help};
    Algorithm algorithm {Algorithm::Huffman};
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads {0};
};

void printUsage()
{
    std::cout << "Usage:\n"
              << "  gsea help\n"
              << "  gsea compress --algo <huffman|lzw> --input <path> --output <path> [--threads <n>]\n"
              << "  gsea decompress --algo <huffman|lzw> --input <path> --output <path> [--threads <n>]\n\n"
              << "Notes:\n"
              << "  - For compression, the input may be a single file or a directory.\n"
              << "  - When decompressing, the CLI inspects the source magic to decide if it is\n"
              << "    an archive (directory) or a single-file payload.\n"
              << "  - Thread count applies to directory operations; 0 uses the default pool size.\n";
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

Algorithm parseAlgorithm(const std::string& name)
{
    const auto lowered = toLower(name);
    if (lowered == "huffman") {
        return Algorithm::Huffman;
    }
    if (lowered == "lzw") {
        return Algorithm::LZW;
    }
    throw std::invalid_argument("Unsupported algorithm: " + name);
}

Command parseCommand(const std::string& argument)
{
    const auto lowered = toLower(argument);
    if (lowered == "compress") {
        return Command::Compress;
    }
    if (lowered == "decompress") {
        return Command::Decompress;
    }
    if (lowered == "help" || lowered == "--help" || lowered == "-h") {
        return Command::Help;
    }
    throw std::invalid_argument("Unknown command: " + argument);
}

std::string readMagic(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file to inspect magic: " + path.string());
    }
    char magic[4] = {0, 0, 0, 0};
    input.read(magic, sizeof(magic));
    if (input.gcount() != static_cast<std::streamsize>(sizeof(magic))) {
        throw std::runtime_error("Unable to read magic header from: " + path.string());
    }
    return std::string(magic, sizeof(magic));
}

Options parseOptions(int argc, char** argv)
{
    Options options {};

    if (argc < 2) {
        options.command = Command::Help;
        return options;
    }

    options.command = parseCommand(argv[1]);
    if (options.command == Command::Help) {
        return options;
    }

    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];

        if (argument == "--algo" && index + 1 < argc) {
            options.algorithm = parseAlgorithm(argv[++index]);
        } else if ((argument == "--input" || argument == "-i") && index + 1 < argc) {
            options.input = std::filesystem::path(argv[++index]);
        } else if ((argument == "--output" || argument == "-o") && index + 1 < argc) {
            options.output = std::filesystem::path(argv[++index]);
        } else if ((argument == "--threads" || argument == "-t") && index + 1 < argc) {
            const std::string value = argv[++index];
            try {
                options.threads = std::stoul(value);
            } catch (const std::exception&) {
                throw std::invalid_argument("Invalid thread count: " + value);
            }
        } else if (argument == "--help" || argument == "-h") {
            options.command = Command::Help;
            return options;
        } else {
            throw std::invalid_argument("Unrecognized argument: " + argument);
        }
    }

    if (options.input.empty()) {
        throw std::invalid_argument("Missing required --input argument");
    }
    if (options.output.empty()) {
        throw std::invalid_argument("Missing required --output argument");
    }

    return options;
}

void compressWithAlgorithm(const Options& options)
{
    if (!std::filesystem::exists(options.input)) {
        throw std::runtime_error("Input path does not exist: " + options.input.string());
    }

    const bool isDirectory = std::filesystem::is_directory(options.input);

    switch (options.algorithm) {
    case Algorithm::Huffman:
        if (isDirectory) {
            gesa::compression::huffman::compressDirectory(options.input, options.output, options.threads);
        } else {
            gesa::compression::huffman::compressFile(options.input, options.output);
        }
        break;
    case Algorithm::LZW:
        if (isDirectory) {
            gesa::compression::lzw::compressDirectory(options.input, options.output, options.threads);
        } else {
            gesa::compression::lzw::compressFile(options.input, options.output);
        }
        break;
    }
}

void decompressWithAlgorithm(const Options& options)
{
    if (!std::filesystem::exists(options.input)) {
        throw std::runtime_error("Input path does not exist: " + options.input.string());
    }
    if (std::filesystem::is_directory(options.input)) {
        throw std::runtime_error("Decompression input must be a file, not a directory");
    }

    const auto magic = readMagic(options.input);

    switch (options.algorithm) {
    case Algorithm::Huffman:
        if (magic == std::string{"GHUF", 4}) {
            gesa::compression::huffman::decompressFile(options.input, options.output);
        } else if (magic == std::string{"GHAR", 4}) {
            gesa::compression::huffman::decompressDirectory(options.input, options.output, options.threads);
        } else {
            throw std::runtime_error("Unrecognized Huffman magic header in input file");
        }
        break;
    case Algorithm::LZW:
        if (magic == std::string{"GLZW", 4}) {
            gesa::compression::lzw::decompressFile(options.input, options.output);
        } else if (magic == std::string{"GLZA", 4}) {
            gesa::compression::lzw::decompressDirectory(options.input, options.output, options.threads);
        } else {
            throw std::runtime_error("Unrecognized LZW magic header in input file");
        }
        break;
    }
}

} // namespace

namespace gesa::cli {

int run(int argc, char** argv)
{
    try {
        const auto options = parseOptions(argc, argv);

        if (options.command == Command::Help) {
            printUsage();
            return 0;
        }

        if (options.command == Command::Compress) {
            compressWithAlgorithm(options);
            std::cout << "Compression completed successfully\n";
            return 0;
        }

        if (options.command == Command::Decompress) {
            decompressWithAlgorithm(options);
            std::cout << "Decompression completed successfully\n";
            return 0;
        }

        printUsage();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}

} // namespace gesa::cli
