#include "Session.h"
#include "ServerManager.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>


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


// Dev 1 (Day 4-5) replaces this with real parsing + the full FTP
static void handle_command(int fd, const char* line) {
    printf("[Session] -> %s\n", line);

    char command[16];
    first_word_upper(line, command, sizeof(command));

    if (strcmp(command, "QUIT") == 0) {
        send_reply(fd, "221 Goodbye.\r\n");
    } else if (strcmp(command, "NOOP") == 0) {
        send_reply(fd, "200 NOOP OK.\r\n");
    } else if (strcmp(command, "USER") == 0) {
        send_reply(fd, "331 Username OK, need password.\r\n");
    } else if (strcmp(command, "PASS") == 0) {
        send_reply(fd, "230 Login successful (stub - no real auth yet).\r\n");
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
        if (n == 0) continue;  // ignore blank lines

        handle_command(fd, line);

        first_word_upper(line, command, sizeof(command));
        if (strcmp(command, "QUIT") == 0) break;
    }

    close(fd);
    clients_remove(args->state, fd);
    free(args);  
    return nullptr;
}