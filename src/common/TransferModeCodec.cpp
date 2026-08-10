#include "TransferModeCodec.h"

#include <algorithm>

namespace {
constexpr uint8_t kEof = 0x40;
constexpr uint8_t kEscape = 0xff;

bool fail(std::string *error, const char *message) {
  if (error)
    *error = message;
  return false;
}
} // namespace

std::vector<uint8_t>
TransferModeCodec::encode(const std::vector<uint8_t> &input,
                          TransferMode mode) {
  if (mode == TransferMode::Stream)
    return input;

  std::vector<uint8_t> result;
  if (mode == TransferMode::Block) {
    // RFC 959 block header: descriptor (1 byte), byte count (2 bytes).
    if (input.empty())
      return {kEof, 0, 0};

    for (size_t offset = 0; offset < input.size();) {
      const size_t count = std::min<size_t>(65535, input.size() - offset);
      const bool last = offset + count == input.size();
      result.push_back(last ? kEof : 0);
      result.push_back(static_cast<uint8_t>(count >> 8));
      result.push_back(static_cast<uint8_t>(count));
      result.insert(result.end(), input.begin() + offset,
                    input.begin() + offset + count);
      offset += count;
    }
    return result;
  }

  // RFC 959 compressed mode. Use repeated strings for runs of at least three
  // bytes and literal strings for everything else. RDT supplies record
  // boundaries, while the escaped EOF descriptor terminates the FTP stream.
  for (size_t pos = 0; pos < input.size();) {
    size_t run = 1;
    while (pos + run < input.size() && input[pos + run] == input[pos] &&
           run < 63)
      ++run;

    if (run >= 3) {
      result.push_back(static_cast<uint8_t>(0x80 | run));
      result.push_back(input[pos]);
      pos += run;
      continue;
    }

    const size_t literalStart = pos;
    size_t literalCount = 0;
    while (pos < input.size() && literalCount < 63) {
      run = 1;
      while (pos + run < input.size() && input[pos + run] == input[pos] &&
             run < 63)
        ++run;
      if (run >= 3)
        break;
      const size_t take = std::min(run, size_t{63} - literalCount);
      pos += take;
      literalCount += take;
    }
    result.push_back(static_cast<uint8_t>(literalCount));
    result.insert(result.end(), input.begin() + literalStart,
                  input.begin() + literalStart + literalCount);
  }
  result.push_back(kEscape);
  result.push_back(kEof);
  return result;
}

bool TransferModeCodec::decode(const std::vector<uint8_t> &input,
                               TransferMode mode, TransferType type,
                               std::vector<uint8_t> &output,
                               std::string *error) {
  output.clear();
  if (mode == TransferMode::Stream) {
    output = input;
    return true;
  }

  size_t pos = 0;
  bool eof = false;
  if (mode == TransferMode::Block) {
    while (pos < input.size()) {
      if (input.size() - pos < 3)
        return fail(error, "truncated MODE B header");
      const uint8_t descriptor = input[pos++];
      const size_t count = (static_cast<size_t>(input[pos]) << 8) |
                           static_cast<size_t>(input[pos + 1]);
      pos += 2;
      if (count > input.size() - pos)
        return fail(error, "truncated MODE B payload");
      output.insert(output.end(), input.begin() + pos,
                    input.begin() + pos + count);
      pos += count;
      if ((descriptor & kEof) != 0) {
        eof = true;
        break;
      }
    }
    if (!eof)
      return fail(error, "MODE B stream has no EOF block");
    if (pos != input.size())
      return fail(error, "data found after MODE B EOF block");
    return true;
  }

  while (pos < input.size()) {
    const uint8_t control = input[pos++];
    if (control == kEscape) {
      if (pos >= input.size())
        return fail(error, "truncated MODE C escape");
      const uint8_t descriptor = input[pos++];
      if ((descriptor & kEof) != 0) {
        eof = true;
        break;
      }
      continue; // EOR and other record descriptors carry no file bytes.
    }

    const size_t count = control & 0x3f;
    if ((control & 0xc0) == 0) {
      if (count == 0 || count > input.size() - pos)
        return fail(error, "invalid MODE C literal string");
      output.insert(output.end(), input.begin() + pos,
                    input.begin() + pos + count);
      pos += count;
    } else if ((control & 0xc0) == 0x80) {
      if (count == 0 || pos >= input.size())
        return fail(error, "invalid MODE C repeated string");
      output.insert(output.end(), count, input[pos++]);
    } else {
      if (count == 0)
        return fail(error, "invalid MODE C filler string");
      output.insert(output.end(), count,
                    type == TransferType::ASCII ? uint8_t{' '} : uint8_t{0});
    }
  }
  if (!eof)
    return fail(error, "MODE C stream has no EOF descriptor");
  if (pos != input.size())
    return fail(error, "data found after MODE C EOF descriptor");
  return true;
}
