#pragma once
 
struct ServerState;

struct SessionArgs {
    ServerState* state;
    int socketFd;
    char ip[46];
    int port;
};
 

void* handle_client_thread(void* argPtr);
 