#include "PathManager.h"

#include <system_error>
#include <algorithm>
#include <optional>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string sanitizeListingName(std::string name)
{
    // LIST/STAT records and NLST names are separated by CRLF.  Do not let a
    // filename inject an extra FTP reply line on the control connection.
    std::replace(name.begin(), name.end(), '\r', '?');
    std::replace(name.begin(), name.end(), '\n', '?');
    return name;
}

std::optional<std::string> formatListEntry(
    const fs::directory_entry& entry
)
{
    std::error_code error;
    const fs::file_status status = entry.status(error);

    if (error) {
        return std::nullopt;
    }

    const bool isDirectory = fs::is_directory(status);
    const bool isFile = fs::is_regular_file(status);

    if (!isDirectory && !isFile) {
        return std::nullopt;
    }

    std::uintmax_t size = 0;

    if (isFile) {
        size = entry.file_size(error);

        if (error) {
            return std::nullopt;
        }
    }

    const fs::perms permissions = status.permissions();
    const fs::perms permissionBits[] = {
        fs::perms::owner_read, fs::perms::owner_write,
        fs::perms::owner_exec, fs::perms::group_read,
        fs::perms::group_write, fs::perms::group_exec,
        fs::perms::others_read, fs::perms::others_write,
        fs::perms::others_exec
    };
    constexpr char permissionChars[] = "rwxrwxrwx";

    std::string permissionText;
    permissionText.reserve(9);

    for (std::size_t index = 0; index < 9; ++index) {
        permissionText += (permissions & permissionBits[index])
            != fs::perms::none ? permissionChars[index] : '-';
    }

    std::ostringstream output;
    output
        << "name=" << sanitizeListingName(entry.path().filename().string())
        << ", type=" << (isDirectory ? "directory" : "file")
        << ", size=" << size
        << ", permissions=" << permissionText;
    return output.str();
}

} // namespace

PathManager::PathManager(const std::string& root)
{
    std::error_code error;

    fs::create_directories(root, error);

    rootDirectory = fs::weakly_canonical(root, error);

    if (error) {
        rootDirectory = fs::absolute(root).lexically_normal();
    }

    currentDirectory = rootDirectory;
}

bool PathManager::isInsideRoot(const fs::path& path) const
{
    const fs::path normalizedPath =
        path.lexically_normal();

    auto rootIterator = rootDirectory.begin();
    auto pathIterator = normalizedPath.begin();

    while (rootIterator != rootDirectory.end()) {
        if (
            pathIterator == normalizedPath.end()
            || *rootIterator != *pathIterator
        ) {
            return false;
        }

        ++rootIterator;
        ++pathIterator;
    }

    return true;
}

fs::path PathManager::resolvePath(
    const std::string& input
) const {
    if (input.empty()) {
        return currentDirectory;
    }

    fs::path requestedPath(input);
    fs::path result;

    if (requestedPath.is_absolute()) {
        result =
            rootDirectory
            / requestedPath.relative_path();
    }
    else {
        result = currentDirectory / requestedPath;
    }

    return result.lexically_normal();
}

bool PathManager::changeDirectory(
    const std::string& path
) {
    std::error_code error;

    const fs::path target = resolvePath(path);

    if (!isInsideRoot(target)) {
        return false;
    }

    if (
        !fs::exists(target, error)
        || error
        || !fs::is_directory(target, error)
        || error
    ) {
        return false;
    }

    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (error || !isInsideRoot(canonicalTarget)) {
        return false;
    }

    currentDirectory = canonicalTarget;
    return true;
}

bool PathManager::changeToParentDirectory()
{
    if (currentDirectory == rootDirectory) {
        return false;
    }

    return changeDirectory("..");
}

std::string PathManager::getCurrentFTPPath() const
{
    if (currentDirectory == rootDirectory) {
        return "/";
    }

    std::error_code error;

    const fs::path relativePath = fs::relative(
        currentDirectory,
        rootDirectory,
        error
    );

    if (error) {
        return "/";
    }

    return "/" + relativePath.generic_string();
}
bool PathManager::createDirectory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    const fs::path target = resolvePath(path);

    // Không cho tạo thư mục ngoài server_storage
    if (!isInsideRoot(target)) {
        return false;
    }

    // Không cho tạo nếu đã tồn tại
    if (fs::exists(target, error) || error) {
        return false;
    }

    return fs::create_directory(target, error) && !error;
}
bool PathManager::removeDirectory(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    const fs::path target = resolvePath(path);

    // Không được truy cập ngoài server_storage
    if (!isInsideRoot(target)) {
        return false;
    }

    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (error || !isInsideRoot(canonicalTarget)) {
        return false;
    }

    // Không được xóa thư mục gốc hoặc thư mục đang đứng
    if (
        canonicalTarget == rootDirectory ||
        canonicalTarget == currentDirectory
    ) {
        return false;
    }

    if (
        !fs::exists(canonicalTarget, error) ||
        error ||
        !fs::is_directory(canonicalTarget, error) ||
        error
    ) {
        return false;
    }

    // fs::remove chỉ xóa được thư mục rỗng
    return fs::remove(canonicalTarget, error) && !error;
}
std::optional<std::string> PathManager::listDirectory(
    const std::string& path
) const
{
    std::error_code error;

    const fs::path target = path.empty()
        ? currentDirectory
        : resolvePath(path);

    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    if (fs::is_regular_file(canonicalTarget, error)) {
        if (error) {
            return std::nullopt;
        }

        const auto line = formatListEntry(fs::directory_entry(canonicalTarget));
        return line.has_value()
            ? std::optional<std::string>{line.value() + "\r\n"}
            : std::nullopt;
    }

    if (error || !fs::is_directory(canonicalTarget, error) || error) {
        return std::nullopt;
    }

    std::vector<fs::directory_entry> entries;

    for (
        fs::directory_iterator iterator(canonicalTarget, error);
        !error && iterator != fs::directory_iterator();
        iterator.increment(error)
    ) {
        entries.push_back(*iterator);
    }

    if (error) {
        return std::nullopt;
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const auto& left, const auto& right) {
            return left.path().filename().string()
                < right.path().filename().string();
        }
    );

    std::ostringstream output;

    for (const auto& entry : entries) {
        const auto line = formatListEntry(entry);

        if (!line.has_value()) {
            return std::nullopt;
        }

        output << line.value() << "\r\n";
    }

    return output.str();
}
std::optional<std::string> PathManager::listNames(
    const std::string& path
) const
{
    std::error_code error;

    const fs::path target = path.empty()
        ? currentDirectory
        : resolvePath(path);

    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    /*
     * Nếu đường dẫn trỏ trực tiếp tới một file,
     * NLST trả về tên file đó.
     */
    if (fs::is_regular_file(canonicalTarget, error)) {
        if (error) {
            return std::nullopt;
        }

        return sanitizeListingName(canonicalTarget.filename().string())
            + "\r\n";
    }

    if (
        !fs::is_directory(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    std::vector<std::string> names;

    for (
        fs::directory_iterator iterator(canonicalTarget, error);
        !error && iterator != fs::directory_iterator();
        iterator.increment(error)
    ) {
        names.push_back(
            iterator->path().filename().string()
        );
    }

    if (error) {
        return std::nullopt;
    }

    std::sort(names.begin(), names.end());

    std::ostringstream output;

    for (const auto& name : names) {
        output << sanitizeListingName(name) << "\r\n";
    }

    return output.str();
}
std::optional<PathStatus> PathManager::getStatus(
    const std::string& path
) const
{
    if (path.empty()) {
        return std::nullopt;
    }

    std::error_code error;

    const fs::path target = resolvePath(path);
    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    const bool isDirectory =
        fs::is_directory(canonicalTarget, error);

    if (error) {
        return std::nullopt;
    }

    const bool isFile =
        fs::is_regular_file(canonicalTarget, error);

    if (error || (!isDirectory && !isFile)) {
        return std::nullopt;
    }

    const auto listing = listDirectory(path);

    if (!listing.has_value()) {
        return std::nullopt;
    }

    return PathStatus{isDirectory, listing.value()};
}
std::optional<std::uintmax_t> PathManager::getFileSize(
    const std::string& path
) const
{
    if (path.empty()) {
        return std::nullopt;
    }

    std::error_code error;

    const fs::path target = resolvePath(path);
    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error ||
        !fs::is_regular_file(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    const std::uintmax_t size =
        fs::file_size(canonicalTarget, error);

    if (error) {
        return std::nullopt;
    }

    return size;
}
std::optional<std::string> PathManager::getModificationTime(
    const std::string& path
) const
{
    if (path.empty()) {
        return std::nullopt;
    }

    std::error_code error;

    const fs::path target = resolvePath(path);
    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error ||
        !fs::is_regular_file(canonicalTarget, error) ||
        error
    ) {
        return std::nullopt;
    }

    const fs::file_time_type fileTime =
        fs::last_write_time(canonicalTarget, error);

    if (error) {
        return std::nullopt;
    }

    /*
     * Chuyển filesystem clock sang system clock.
     */
    const auto systemTime =
        std::chrono::time_point_cast<
            std::chrono::system_clock::duration
        >(
            fileTime -
            fs::file_time_type::clock::now() +
            std::chrono::system_clock::now()
        );

    const std::time_t rawTime =
        std::chrono::system_clock::to_time_t(systemTime);

    std::tm utcTime{};

#ifdef _WIN32
    gmtime_s(&utcTime, &rawTime);
#else
    gmtime_r(&rawTime, &utcTime);
#endif

    std::ostringstream output;

    output << std::put_time(
        &utcTime,
        "%Y%m%d%H%M%S"
    );

    return output.str();
}
bool PathManager::deleteFile(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code error;

    const fs::path target = resolvePath(path);

    if (!isInsideRoot(target)) {
        return false;
    }

    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error
    ) {
        return false;
    }

    /*
     * DELE chỉ được xóa file thường.
     * Thư mục phải dùng RMD.
     */
    if (
        !fs::is_regular_file(canonicalTarget, error) ||
        error
    ) {
        return false;
    }

    return fs::remove(canonicalTarget, error) && !error;
}
bool PathManager::canRename(
    const std::string& path
) const
{
    if (path.empty()) {
        return false;
    }

    std::error_code error;

    const fs::path target = resolvePath(path);
    const fs::path canonicalTarget =
        fs::weakly_canonical(target, error);

    if (
        error ||
        !isInsideRoot(canonicalTarget) ||
        !fs::exists(canonicalTarget, error) ||
        error
    ) {
        return false;
    }

    const bool isFile =
        fs::is_regular_file(canonicalTarget, error);

    if (error) {
        return false;
    }

    const bool isDirectory =
        fs::is_directory(canonicalTarget, error);

    if (error || (!isFile && !isDirectory)) {
        return false;
    }

    // Không cho đổi tên thư mục gốc.
    if (canonicalTarget == rootDirectory) {
        return false;
    }

    /*
     * Không đổi tên thư mục hiện tại hoặc thư mục cha
     * của thư mục hiện tại vì sẽ làm currentDirectory
     * mất hiệu lực.
     */
    if (isDirectory) {
        const fs::path relativeCurrent =
            fs::relative(
                currentDirectory,
                canonicalTarget,
                error
            );

        if (error) {
            return false;
        }

        const auto firstPart = relativeCurrent.begin();

        if (
            firstPart == relativeCurrent.end() ||
            *firstPart != ".."
        ) {
            return false;
        }
    }

    return true;
}
bool PathManager::renamePath(
    const std::string& oldPath,
    const std::string& newPath
) {
    if (
        oldPath.empty() ||
        newPath.empty() ||
        !canRename(oldPath)
    ) {
        return false;
    }

    std::error_code error;

    const fs::path source =
        fs::weakly_canonical(
            resolvePath(oldPath),
            error
        );

    if (
        error ||
        !isInsideRoot(source)
    ) {
        return false;
    }

    const fs::path destination =
        resolvePath(newPath);

    if (
        destination.filename().empty() ||
        !isInsideRoot(destination)
    ) {
        return false;
    }

    /*
     * Không ghi đè file/thư mục đã tồn tại.
     */
    if (
        fs::exists(destination, error) ||
        error
    ) {
        return false;
    }

    /*
     * Thư mục cha của đích phải tồn tại
     * và phải nằm trong server_storage.
     */
    const fs::path destinationParent =
        fs::weakly_canonical(
            destination.parent_path(),
            error
        );

    if (
        error ||
        !isInsideRoot(destinationParent) ||
        !fs::exists(destinationParent, error) ||
        error ||
        !fs::is_directory(destinationParent, error) ||
        error
    ) {
        return false;
    }

    fs::rename(source, destination, error);

    return !error;
}
