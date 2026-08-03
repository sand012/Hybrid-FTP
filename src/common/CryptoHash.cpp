#include "CryptoHash.h"
#include "FileHandler.h"
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>

std::string CryptoHash::computeSHA256(const std::vector<char>& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)data.data(), data.size(), hash);

    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

std::string CryptoHash::computeSHA256FromFile(const std::string& filepath) {
    std::vector<char> data;
    if (!FileHandler::readBinaryFile(filepath, data)) {
        return "";
    }
    return computeSHA256(data);
}