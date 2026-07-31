#pragma once

#include <pthread.h>

#define MAX_CLIENTS 100


struct ClientRecord {
    int socketFd;
    char ip[46];
    int port;
    int active;  
};


struct ServerState {
    int listenFd;
    int port;
    int running;  

    pthread_mutex_t clientsLock;  
    ClientRecord clients[MAX_CLIENTS];
};


// Returns 0 on success, -1 on failure.
int server_init(ServerState* state, int port);


void server_run(ServerState* state);

void server_stop(ServerState* state);

int  clients_add(ServerState* state, int socketFd, const char* ip, int port);
void clients_remove(ServerState* state, int socketFd);
void clients_print(ServerState* state);