#include "FileHandler.h"
#include <fstream>
#include <sys/stat.h>

bool FileHandler::readBinaryFile(const std::string& filepath, std::vector<char>& buffer) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    buffer.resize(size);
    if (file.read(buffer.data(), size)) {
        return true;
    }
    return false;
}

bool FileHandler::writeBinaryFile(const std::string& filepath, const char* data, size_t size, bool append) {
    std::ofstream file(filepath, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!file.is_open()) return false;
    
    file.write(data, size);
    return file.good();
}

bool FileHandler::readTextFile(const std::string& filepath, std::string& content) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    
    content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

bool FileHandler::writeTextFile(const std::string& filepath, const std::string& content, bool append) {
    std::ofstream file(filepath, std::ios::out | (append ? std::ios::app : std::ios::trunc));
    if (!file.is_open()) return false;
    
    file << content;
    return file.good();
}

bool FileHandler::fileExists(const std::string& filepath) {
    struct stat buffer;
    return (stat(filepath.c_str(), &buffer) == 0);
}

size_t FileHandler::getFileSize(const std::string& filepath) {
    struct stat buffer;
    if (stat(filepath.c_str(), &buffer) == 0) {
        return buffer.st_size;
    }
    return 0;
}