#include "TransferModeCodec.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
  const std::vector<std::vector<uint8_t>> cases = {
      {},
      {'h', 'e', 'l', 'l', 'o'},
      {0, 0, 0, 0, 1, 2, 3, 255, 255, 255},
      std::vector<uint8_t>(70000, 42),
  };

  for (const TransferMode mode : {TransferMode::Stream, TransferMode::Block,
                                  TransferMode::Compressed}) {
    for (const auto &input : cases) {
      const auto encoded = TransferModeCodec::encode(input, mode);
      std::vector<uint8_t> decoded;
      std::string error;
      assert(TransferModeCodec::decode(encoded, mode, TransferType::Binary,
                                       decoded, &error));
      assert(decoded == input);
    }
  }

  std::vector<uint8_t> output;
  assert(!TransferModeCodec::decode({0, 0, 4, 1}, TransferMode::Block,
                                    TransferType::Binary, output));
  assert(!TransferModeCodec::decode({3, 1}, TransferMode::Compressed,
                                    TransferType::Binary, output));
}
