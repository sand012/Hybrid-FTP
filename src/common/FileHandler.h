#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include <string>
#include <vector>

class FileHandler {
public:
    // Đọc file nhị phân (Binary mode)
    static bool readBinaryFile(const std::string& filepath, std::vector<char>& buffer);
    
    // Ghi file nhị phân (Binary mode)
    static bool writeBinaryFile(const std::string& filepath, const char* data, size_t size);
    
    // Đọc file văn bản (ASCII mode)
    static bool readTextFile(const std::string& filepath, std::string& content);
    
    // Ghi file văn bản (ASCII mode)
    static bool writeTextFile(const std::string& filepath, const std::string& content);
    
    // Kiểm tra file có tồn tại hay không
    static bool fileExists(const std::string& filepath);
    
    // Lấy kích thước file
    static size_t getFileSize(const std::string& filepath);
};

#endif 