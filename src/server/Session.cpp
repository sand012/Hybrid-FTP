#include "Session.h"
#include "ServerManager.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

// (Active/Passive mode) 
struct DataConnectionState {
    bool isPassive = false;
    std::string remoteIp = "";
    int remotePort = 0;
    int passiveSocketFd = -1;
};

static DataConnectionState dataState;

static int read_line(int fd, char* buffer, int bufferSize) {
    int len = 0;
    while (len < bufferSize - 1) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) return -1;  // connection closed or error
        if (c == '\n') break;
        if (c != '\r') {
            buffer[len] = c;
            len++;
        }
    }
    buffer[len] = '\0';
    return len;
}

static void send_reply(int fd, const char* reply) {
    send(fd, reply, strlen(reply), 0);
}

static void first_word_upper(const char* line, char* outUpper, int outSize) {
    int i = 0;
    while (line[i] != '\0' && line[i] != ' ' && i < outSize - 1) {
        outUpper[i] = (char)toupper((unsigned char)line[i]);
        i++;
    }
    outUpper[i] = '\0';
}

// Phân tích cú pháp lệnh PORT: PORT h1,h2,h3,h4,p1,p2 (Active Mode)
static bool parse_port_command(const char* args, std::string& outIp, int& outPort) {
    int h1, h2, h3, h4, p1, p2;
    char comma;
    std::stringstream ss(args);
    if (ss >> h1 >> comma >> h2 >> comma >> h3 >> comma >> h4 >> comma >> p1 >> comma >> p2) {
        outIp = std::to_string(h1) + "." + std::to_string(h2) + "." + std::to_string(h3) + "." + std::to_string(h4);
        outPort = (p1 << 8) + p2;
        return true;
    }
    return false;
}

static void handle_command(int fd, const char* line) {
    printf("[Session] -> %s\n", line);

    char command[16];
    first_word_upper(line, command, sizeof(command));

    // Tách phần tham số phía sau lệnh (nếu có)
    const char* args = line;
    while (*args != '\0' && !isspace((unsigned char)*args)) args++;
    while (*args != '\0' && isspace((unsigned char)*args)) args++;

    if (strcmp(command, "QUIT") == 0) {
        send_reply(fd, "221 Goodbye.\r\n");
    } else if (strcmp(command, "NOOP") == 0) {
        send_reply(fd, "200 NOOP OK.\r\n");
    } else if (strcmp(command, "USER") == 0) {
        send_reply(fd, "331 Username OK, need password.\r\n");
    } else if (strcmp(command, "PASS") == 0) {
        send_reply(fd, "230 Login successful.\r\n");
    } else if (strcmp(command, "PORT") == 0) {
        if (parse_port_command(args, dataState.remoteIp, dataState.remotePort)) {
            dataState.isPassive = false;
            send_reply(fd, "200 PORT command successful.\r\n");
        } else {
            send_reply(fd, "501 Syntax error in parameters or arguments.\r\n");
        }
    } else if (strcmp(command, "PASV") == 0) {
        dataState.isPassive = true;
        // Phản hồi mẫu Passive Mode chuyển đổi IP và Port cho client kết nối dữ liệu
        send_reply(fd, "227 Entering Passive Mode (127,0,0,1,19,136).\r\n");
    } else {
        send_reply(fd, "502 Command not implemented (Day 4-5 work).\r\n");
    }
}

void* handle_client_thread(void* argPtr) {
    SessionArgs* args = (SessionArgs*)argPtr;
    int fd = args->socketFd;

    send_reply(fd, "220 Hybrid FTP service ready.\r\n");

    char line[512];
    char command[16];

    while (1) {
        int n = read_line(fd, line, sizeof(line));
        if (n < 0) break;   // client disconnected
        if (n == 0) continue;   // ignore blank lines

        handle_command(fd, line);

        first_word_upper(line, command, sizeof(command));
        if (strcmp(command, "QUIT") == 0) break;
    }

    close(fd);
    clients_remove(args->state, fd);
    free(args);  
    return nullptr;
}