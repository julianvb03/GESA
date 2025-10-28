#include "cli/application.hpp"

#include "compression/huffman.hpp"
#include "compression/lzw.hpp"
#include "encryption/RSA.h"

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
#include <vector>

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

enum class EncAlgorithm {
    RSA
};

enum class Operation {
    Compress,
    Decompress,
    Encrypt,
    Decrypt
};

struct Options {
    Command command {Command::Help};
    Algorithm algorithm {Algorithm::Huffman};
    std::filesystem::path input;
    std::filesystem::path output;
    std::size_t threads {0};
    // New encryption/operations options
    std::string opSequence; // e.g. "ce", "du", etc.
    EncAlgorithm encAlgorithm {EncAlgorithm::RSA};
    std::string key; // Base64 key for encrypt/decrypt (public for encrypt, private for decrypt)
};

void printUsage()
{
    std::cout << "Usage:\n"
              << "  gsea help\n"
              << "\n"
              << "  // New unified flags (can be combined):\n"
              << "  gsea -[c|d|e|u]+ --comp-alg <huffman|lzw> --enc-alg <rsa> -i <input> -o <output> [-t <n>] [-k <key>]\n"
              << "    -c: compress   -d: decompress   -e: encrypt   -u: decrypt\n"
              << "    e.g. -ce to compress, then encrypt. -du to decrypt, then decompress.\n"
              << "\n"
              << "  // Back-compat commands (still supported):\n"
              << "  gsea compress --algo <huffman|lzw> --input <path> --output <path> [--threads <n>]\n"
              << "  gsea decompress --algo <huffman|lzw> --input <path> --output <path> [--threads <n>]\n\n"
              << "Notes:\n"
              << "  - For compression, input may be a single file or a directory.\n"
              << "  - When decompressing, the CLI inspects the source magic to decide if it is\n"
              << "    an archive (directory) or a single-file payload.\n"
              << "  - Thread count applies to directory operations; 0 uses the default pool size.\n"
              << "  - Encryption expects files. For directories, use -c before -e (e.g. -ce).\n"
              << "  - -k provides public key (encrypt) or private key (decrypt). If omitted for\n"
              << "    encryption, a keypair is generated and printed.\n";
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

EncAlgorithm parseEncAlgorithm(const std::string& name)
{
    const auto lowered = toLower(name);
    if (lowered == "rsa") {
        return EncAlgorithm::RSA;
    }
    throw std::invalid_argument("Unsupported encryption algorithm: " + name);
}

std::vector<Operation> parseOperations(const std::string& ops)
{
    std::vector<Operation> result;
    for (char ch : ops) {
        switch (ch) {
        case 'c': result.push_back(Operation::Compress); break;
        case 'd': result.push_back(Operation::Decompress); break;
        case 'e': result.push_back(Operation::Encrypt); break;
        case 'u': result.push_back(Operation::Decrypt); break;
        default:
            throw std::invalid_argument(std::string{"Unknown operation flag: -"} + ch);
        }
    }
    return result;
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

    // New mode: combined short flags, start with '-' and a sequence of c/d/e/u
    const std::string firstArg = argv[1];
    const bool isOpsMode = (!firstArg.empty() && firstArg[0] == '-' && firstArg.find_first_of("cdeu") != std::string::npos);

    if (!isOpsMode) {
        // Backward compatible mode
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

    // New combined-ops mode
    options.command = Command::Help; // not used in this mode
    options.opSequence = firstArg.substr(1); // skip leading '-'

    for (int index = 2; index < argc; ++index) {
        const std::string argument = argv[index];
        if ((argument == "--comp-alg" || argument == "--algo") && index + 1 < argc) {
            options.algorithm = parseAlgorithm(argv[++index]);
        } else if (argument == "--enc-alg" && index + 1 < argc) {
            options.encAlgorithm = parseEncAlgorithm(argv[++index]);
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
        } else if ((argument == "--key" || argument == "-k") && index + 1 < argc) {
            options.key = argv[++index];
        } else if (argument == "--help" || argument == "-h") {
            options.command = Command::Help;
            return options;
        } else {
            throw std::invalid_argument("Unrecognized argument: " + argument);
        }
    }

    if (options.input.empty()) {
        throw std::invalid_argument("Missing required -i/--input argument");
    }
    if (options.output.empty()) {
        throw std::invalid_argument("Missing required -o/--output argument");
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

std::vector<Operation> getOperations(const Options& options)
{
    if (options.opSequence.empty()) {
        return {};
    }
    return parseOperations(options.opSequence);
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file for reading: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(size);
    if (size > 0) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (!input) {
            throw std::runtime_error("Failed to read file contents: " + path.string());
        }
    }
    return data;
}

void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data)
{
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

void encryptFileRSA(const std::filesystem::path& input,
                    const std::filesystem::path& output,
                    const std::string& maybePublicKey)
{
    Rsa rsa(61, 53); // demo primes; n >= 256
    std::string publicKey = maybePublicKey;
    if (publicKey.empty()) {
        const auto keys = rsa.generateKeys();
        std::cout << "Generated RSA keypair:\n"
                  << "  Public (-k for encrypt):  " << keys.publicKey << "\n"
                  << "  Private (-k for decrypt): " << keys.privateKey << "\n";
        publicKey = keys.publicKey;
        Utils::freeCString(keys.publicKey);
        Utils::freeCString(keys.privateKey);
    }
    const auto plain = readFileBytes(input);
    const auto cipher = rsa.encrypt(plain, publicKey);
    writeFileBytes(output, cipher);
}

void decryptFileRSA(const std::filesystem::path& input,
                    const std::filesystem::path& output,
                    const std::string& privateKey)
{
    if (privateKey.empty()) {
        throw std::invalid_argument("Missing -k <private_key> for decryption");
    }
    Rsa rsa(61, 53);
    const auto cipher = readFileBytes(input);
    const auto plain = rsa.decrypt(cipher, privateKey);
    writeFileBytes(output, plain);
}

void executeOperations(const Options& options)
{
    auto ops = getOperations(options);
    if (ops.empty()) {
        printUsage();
        return;
    }

    std::filesystem::path currentInput = options.input;
    std::filesystem::path finalOutput = options.output;
    std::filesystem::path tempPath = finalOutput;
    tempPath += ".tmp";

    for (std::size_t idx = 0; idx < ops.size(); ++idx) {
        const bool isLast = (idx + 1 == ops.size());
        const auto outPath = isLast ? finalOutput : tempPath;

        switch (ops[idx]) {
        case Operation::Compress: {
            Options local = options;
            local.input = currentInput;
            local.output = outPath;
            compressWithAlgorithm(local);
            currentInput = outPath;
            break;
        }
        case Operation::Decompress: {
            Options local = options;
            local.input = currentInput;
            local.output = outPath;
            decompressWithAlgorithm(local);
            currentInput = outPath;
            break;
        }
        case Operation::Encrypt: {
            if (std::filesystem::is_directory(currentInput)) {
                throw std::runtime_error("Encryption expects a file. Compress directories first (use -c before -e).");
            }
            switch (options.encAlgorithm) {
            case EncAlgorithm::RSA:
                encryptFileRSA(currentInput, outPath, options.key);
                break;
            }
            std::cout << "Encriptación completada" << "\n";
            currentInput = outPath;
            break;
        }
        case Operation::Decrypt: {
            if (std::filesystem::is_directory(currentInput)) {
                throw std::runtime_error("Decryption expects a file output.");
            }
            switch (options.encAlgorithm) {
            case EncAlgorithm::RSA:
                decryptFileRSA(currentInput, outPath, options.key);
                break;
            }
            std::cout << "Desencriptación completada" << "\n";
            currentInput = outPath;
            break;
        }
        }
    }
}

} // namespace

namespace gesa::cli {

int run(int argc, char** argv)
{
    try {
        const auto options = parseOptions(argc, argv);

        if (!options.opSequence.empty()) {
            executeOperations(options);
            std::cout << "Operations completed successfully\n";
            return 0;
        }

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
