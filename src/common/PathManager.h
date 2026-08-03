#pragma once

#include <filesystem>
#include <string>
#include<optional>
#include <cstdint>
#include <optional>
#include <string>

class PathManager {
private:
    std::filesystem::path rootDirectory;
    std::filesystem::path currentDirectory;

    bool isInsideRoot(
        const std::filesystem::path& path
    ) const;

public:
    explicit PathManager(
        const std::string& root = "server_storage"
    );
    bool createDirectory(const std::string& path);
    bool removeDirectory(const std::string& path);
    std::string getCurrentFTPPath() const;
    std::optional<std::string> listDirectory(
    const std::string& path = ""
) const;

std::optional<std::string> listNames(
    const std::string& path = ""
) const;

std::optional<std::string> getStatus(
    const std::string& path
) const;

std::optional<std::uintmax_t> getFileSize(
    const std::string& path
) const;

std::optional<std::string> getModificationTime(
    const std::string& path
) const;
    std::filesystem::path resolvePath(
        const std::string& input
    ) const;
    
    bool changeDirectory(const std::string& path);
    bool changeToParentDirectory();
};