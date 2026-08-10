#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class TransferType { ASCII, Binary };
enum class TransferMode { Stream, Block, Compressed };

class TransferModeCodec {
public:
  static std::vector<uint8_t> encode(const std::vector<uint8_t> &input,
                                     TransferMode mode);

  // Returns false for a malformed or incomplete MODE B/C stream.
  static bool decode(const std::vector<uint8_t> &input, TransferMode mode,
                     TransferType type, std::vector<uint8_t> &output,
                     std::string *error = nullptr);
};
