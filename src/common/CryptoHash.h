#ifndef CRYPTO_HASH_H
#define CRYPTO_HASH_H

#include <string>
#include <vector>

class CryptoHash {
public:
    // Tính mã băm SHA-256 cho dữ liệu dạng chuỗi hoặc mảng byte
    static std::string computeSHA256(const std::vector<char>& data);
    static std::string computeSHA256FromFile(const std::string& filepath);
};

#endif // CRYPTO_HASH_H