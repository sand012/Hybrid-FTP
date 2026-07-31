#include "ServerManager.h"
#include "Session.h"

#include <arpa/inet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int server_init(ServerState* state, int port) {
    state->port = port;
    state->running = 0;
    pthread_mutex_init(&state->clientsLock, nullptr);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        state->clients[i].active = 0;
    }

    state->listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (state->listenFd < 0) {
        perror("[Server] socket() failed");
        return -1;
    }

    int opt = 1;
    setsockopt(state->listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(state->listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[Server] bind() failed");
        close(state->listenFd);
        return -1;
    }

    if (listen(state->listenFd, 32) < 0) {
        perror("[Server] listen() failed");
        close(state->listenFd);
        return -1;
    }

    printf("[Server] Hybrid FTP control server listening on TCP port %d\n", port);
    return 0;
}

void server_run(ServerState* state) {
    state->running = 1;

    while (state->running) {
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);

        int clientFd = accept(state->listenFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientFd < 0) {
            if (!state->running) break;  // server_stop() was called - expected
            perror("[Server] accept() failed");
            continue;
        }

        char ipStr[46];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
        int clientPort = ntohs(clientAddr.sin_port);

        printf("[Server] Client connected: %s:%d\n", ipStr, clientPort);
        clients_add(state, clientFd, ipStr, clientPort);

        
        SessionArgs* args = (SessionArgs*)malloc(sizeof(SessionArgs));
        args->state = state;
        args->socketFd = clientFd;
        strncpy(args->ip, ipStr, sizeof(args->ip) - 1);
        args->ip[sizeof(args->ip) - 1] = '\0';
        args->port = clientPort;

        pthread_t tid;
        pthread_create(&tid, nullptr, handle_client_thread, args);
        pthread_detach(tid);  
    }
}

void server_stop(ServerState* state) {
    if (state->running) {
        state->running = 0;
        shutdown(state->listenFd, SHUT_RDWR);  
        close(state->listenFd);
        printf("[Server] Listener stopped.\n");
    }
}

int clients_add(ServerState* state, int socketFd, const char* ip, int port) {
    pthread_mutex_lock(&state->clientsLock);

    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!state->clients[i].active) {
            state->clients[i].active = 1;
            state->clients[i].socketFd = socketFd;
            strncpy(state->clients[i].ip, ip, sizeof(state->clients[i].ip) - 1);
            state->clients[i].ip[sizeof(state->clients[i].ip) - 1] = '\0';
            state->clients[i].port = port;
            slot = i;
            break;
        }
    }

    pthread_mutex_unlock(&state->clientsLock);
    return slot;  // -1 means the table was full (MAX_CLIENTS reached)
}

void clients_remove(ServerState* state, int socketFd) {
    pthread_mutex_lock(&state->clientsLock);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (state->clients[i].active && state->clients[i].socketFd == socketFd) {
            state->clients[i].active = 0;
            break;
        }
    }

    pthread_mutex_unlock(&state->clientsLock);
    printf("[Server] Client disconnected (fd=%d)\n", socketFd);
}

void clients_print(ServerState* state) {
    pthread_mutex_lock(&state->clientsLock);

    printf("---- Active session table ----\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (state->clients[i].active) {
            printf("  %s:%d  fd=%d\n",
                   state->clients[i].ip, state->clients[i].port, state->clients[i].socketFd);
        }
    }
    printf("-------------------------------\n");

    pthread_mutex_unlock(&state->clientsLock);
}