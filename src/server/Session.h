#pragma once

#include <string>
#include "../common/PathManager.h"
struct ServerState;

enum class LoginState {
    NotLoggedIn,
    UsernameAccepted,
    LoggedIn
};

struct SessionState {
    LoginState loginState = LoginState::NotLoggedIn;
    std::string username;
    PathManager pathManager{"server_storage"};
};

struct SessionArgs {
    ServerState* state;
    int socketFd;
    char ip[46];
    int port;
};

void* handle_client_thread(void* argPtr);