#include "ClientCLI.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// NOTE: This is a minimal, temporary TCP connection just so Dev 3 has an
// end-to-end demo (CLI <-> Session) for the Day 1-3 milestone. Dev 1's
// TCPControl module (Day 4-5) is meant to replace connectToServer()/
// sendAndReceive() below with the real control-channel implementation
// (framing, reply-code parsing, etc).

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
        const ssize_t sent = send(
            sock,
            data.data() + sentTotal,
            data.size() - sentTotal,
            0
        );

        if (sent <= 0) {
            return false;
        }

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

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 2121;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);

    int sock = connectToServer(host, port);
    if (sock < 0) {
        return 1;
    }

    // Server sends a 220 welcome banner as soon as we connect.
    std::cout << recvLine(sock) << "\n";

ClientCLI cli([sock](const std::string& commandLine) -> std::string {
    const std::string request = commandLine + "\r\n";

    if (!sendAll(sock, request)) {
        return "Connection error: cannot send command.";
    }

    return recvLine(sock);
});

    cli.run();

    close(sock);
    return 0;
}