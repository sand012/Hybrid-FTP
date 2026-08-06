#pragma once
#include "../common/PathManager.h"
#include <optional>
#include <string>
struct ServerState;
enum class TransferType { ASCII, Binary };

enum class TransferMode { Stream };
enum class LoginState { NotLoggedIn, UsernameAccepted, LoggedIn };

struct SessionState {
  LoginState loginState = LoginState::NotLoggedIn;
  std::string username;
  TransferType transferType = TransferType::ASCII;
  TransferMode transferMode = TransferMode::Stream;
  PathManager pathManager{"server_storage"};

  // Lưu đường dẫn từ lệnh RNFR cho riêng từng client.
  std::optional<std::string> renameFrom;
};
struct SessionArgs {
  ServerState *state;
  int socketFd;
  char ip[46];
  int port;
};

void *handle_client_thread(void *argPtr);