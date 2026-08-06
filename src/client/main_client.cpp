#include "ClientCLI.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// TCP control channel helpers

namespace {

int connectToServer(const std::string& host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket: " << std::strerror(errno) << "\n";
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid server address: " << host << "\n";
        close(sock);
        return -1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        std::cerr << "Connection to " << host << ":" << port
                   << " failed: " << std::strerror(errno) << "\n";
        close(sock);
        return -1;
    }

    return sock;
}

bool sendAll(int sock, const std::string& data) {
    std::size_t sentTotal = 0;
    while (sentTotal < data.size()) {
        const ssize_t sent = send(sock, data.data() + sentTotal,
                                  data.size() - sentTotal, 0);
        if (sent <= 0) return false;
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

std::string recvLine(int sock) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = recv(sock, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            break;
        }
        line.push_back(c);
    }
    return line;
}

std::string recvReply(int sock) {
    const std::string firstLine = recvLine(sock);
    if (firstLine.empty()) return "";

    std::string reply = firstLine;

    // Multi-line reply starts with "XYZ-"
    if (firstLine.size() < 4 || firstLine[3] != '-') {
        return reply;
    }

    const std::string replyCode = firstLine.substr(0, 3);
    while (true) {
        const std::string nextLine = recvLine(sock);
        if (nextLine.empty()) break;
        reply += "\n" + nextLine;
        if (nextLine.rfind(replyCode + " ", 0) == 0) break;
    }
    return reply;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 2121;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);

    int sock = connectToServer(host, port);
    if (sock < 0) return 1;

    // Server sends 220 welcome banner immediately on connect.
    std::cout << recvReply(sock) << "\n";

    ClientCLI cli(
        [sock](const std::string& commandLine) -> std::string {
            const std::string toSend = commandLine + "\r\n";
            if (!sendAll(sock, toSend)) return "Error: Cannot send command.";
            return recvReply(sock);
        },
        sock
    );

    cli.run();

    close(sock);
    return 0;
}