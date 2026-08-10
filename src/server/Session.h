#pragma once
#include "../common/PathManager.h"
#include "../common/TransferModeCodec.h"
#include "../rdt/UDPSocket.h"
#include <memory>
#include <optional>
#include <string>
struct ServerState;
enum class LoginState { NotLoggedIn, UsernameAccepted, LoggedIn };
enum class DataMode { NONE, ACTIVE, PASSIVE };

struct SessionState {
  LoginState loginState = LoginState::NotLoggedIn;
  std::string username;
  TransferType transferType = TransferType::ASCII;
  TransferMode transferMode = TransferMode::Stream;
  PathManager pathManager{"server_storage"};

  // Lưu đường dẫn từ lệnh RNFR cho riêng từng client.
  std::optional<std::string> renameFrom;

  // Trạng thái data channel cho Dev 3
  DataMode dataMode = DataMode::NONE;
  std::string activeIP;
  uint16_t activePort = 0;
  std::unique_ptr<UDPSocket> passiveSocket = nullptr;
};
struct SessionArgs {
  ServerState *state;
  int socketFd;
  char ip[46];
  int port;
};

void *handle_client_thread(void *argPtr);
