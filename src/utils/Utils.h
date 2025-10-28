#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstring>  // Para manejo de strings en C
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <unordered_map>
#include <sstream>

class Utils {
public:
    static std::vector<int> stringToC(const char* str);
    static char* cToString(const std::vector<int>& bytes);
    static void freeCString(char* str);
    static int powerModulus(int base, int expo, int m);
    static int modInverse(int e, int phi);
    static std::vector<uint8_t> serializeNumbers(const std::vector<int>& numbers);
    static std::vector<int> deserializeNumbers(const std::vector<uint8_t>& binaryData);
    static std::string binaryToBase64(const std::vector<uint8_t>& binaryData);
    static std::vector<uint8_t> base64ToBinary(const std::string& base64Str);
    static char* numbersToBase64(const std::vector<int>& numbers);
    static std::vector<int> base64ToNumbers(const char* base64CStr);
    static std::unordered_map<char, int> createFreqMap(const std::vector<char>& data);
};

#endif // UTILS_H