#include "PathManager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

} // namespace

int main() {
  const fs::path root = fs::temp_directory_path() /
                        ("hybrid_ftp_listing_test_" +
                         std::to_string(
                             std::chrono::steady_clock::now()
                                 .time_since_epoch()
                                 .count()));
  std::error_code error;
  fs::create_directories(root / "empty", error);
  fs::create_directories(root / "folder", error);
  std::ofstream(root / "alpha.txt", std::ios::binary) << "abc";

  PathManager paths(root.string());
  bool passed = true;

  const auto names = paths.listNames();
  passed &= expect(names.has_value(), "NLST on the current directory succeeds");
  passed &= expect(names.value_or("") ==
                       "alpha.txt\r\nempty\r\nfolder\r\n",
                   "NLST emits sorted names separated by CRLF");

  const auto directoryListing = paths.listDirectory();
  passed &= expect(directoryListing.has_value() &&
                       directoryListing->find(" | ") == std::string::npos &&
                       directoryListing->ends_with("\r\n"),
                   "LIST emits one CRLF-terminated record per entry");

  const auto emptyNames = paths.listNames("empty");
  passed &= expect(emptyNames.has_value() && emptyNames->empty(),
                   "NLST on an empty directory returns an empty data stream");

  const auto fileListing = paths.listDirectory("alpha.txt");
  passed &= expect(fileListing.has_value(), "LIST accepts a file pathname");
  passed &= expect(fileListing.value_or("").find("name=alpha.txt") == 0 &&
                       fileListing.value_or("").ends_with("\r\n"),
                   "LIST emits a CRLF-terminated file record");

  const auto directoryStatus = paths.getStatus("folder");
  passed &= expect(directoryStatus.has_value() &&
                       directoryStatus->isDirectory &&
                       directoryStatus->listing.empty(),
                   "STAT identifies an empty directory and returns its listing");

  const auto fileStatus = paths.getStatus("alpha.txt");
  passed &= expect(fileStatus.has_value() && !fileStatus->isDirectory &&
                       fileStatus->listing == fileListing,
                   "STAT file data matches LIST file data");

  fs::remove_all(root, error);
  return passed ? 0 : 1;
}
