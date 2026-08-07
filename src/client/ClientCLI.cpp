#include "ClientCLI.h"
#include "../rdt/ReliableTransfer.h"
#include "../rdt/UDPSocket.h"
#include "../common/CryptoHash.h"

#include <arpa/inet.h>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// ============================================================
ClientCLI::ClientCLI(CommandSender sender, int controlSock)
    : m_sendCommand(std::move(sender)), m_controlSock(controlSock)
{
    // Lấy IP server từ TCP socket
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    if (getpeername(controlSock, reinterpret_cast<sockaddr*>(&peer), &len) == 0) {
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, buf, sizeof(buf));
        m_serverHost = buf;
    } else {
        m_serverHost = "127.0.0.1";
    }
}

// ============================================================
//  Parse "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)."
// ============================================================
uint16_t ClientCLI::parsePasvReply(const std::string& reply, std::string& outIP) const
{
    std::regex re(R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))");
    std::smatch m;
    if (!std::regex_search(reply, m, re)) {
        return 0;
    }
    outIP = m[1].str() + "." + m[2].str() + "." + m[3].str() + "." + m[4].str();
    uint16_t p1 = static_cast<uint16_t>(std::stoul(m[5].str()));
    uint16_t p2 = static_cast<uint16_t>(std::stoul(m[6].str()));
    return static_cast<uint16_t>((p1 << 8) | p2);
}

// ============================================================
//  RETR: tải file từ server về máy local
// ============================================================
std::string ClientCLI::handleRetr(const std::string& remoteFile,
                                   const std::string& localFile)
{
    // 1. Gửi PASV để lấy UDP port
    std::string pasvReply = m_sendCommand("PASV");
    if (pasvReply.empty() || pasvReply.rfind("227", 0) != 0) {
        return "Error: PASV failed: " + pasvReply;
    }

    std::string serverIP;
    uint16_t pasvPort = parsePasvReply(pasvReply, serverIP);
    if (pasvPort == 0) {
        return "Error: Cannot parse PASV reply: " + pasvReply;
    }

    // 2. Mở UDP socket phía client
    UDPSocket udp;
    try {
        udp.open();
        udp.bind(0);
        udp.setRecvTimeout(15000);
    } catch (const std::exception& e) {
        return std::string("Error: Cannot open UDP socket: ") + e.what();
    }

    // 3. Gửi RETR lên server (server sẽ chờ client knock trên PASV port trước)
    // Gửi knock packet đến server PASV port để server biết client ở đâu
    {
        // Knock bằng một gói nhỏ để server passive RETR biết địa chỉ client
        uint8_t knock[1] = {0};
        udp.sendTo(knock, sizeof(knock), serverIP, pasvPort);
    }

    std::string retrReply = m_sendCommand("RETR " + remoteFile);
    if (retrReply.empty()) {
        return "Error: No reply to RETR";
    }
    // Có thể là 150 (transfer starting) hoặc 550 (file not found) v.v.
    if (retrReply.rfind("150", 0) != 0) {
        return retrReply; // lỗi (550, 551, v.v.)
    }

    std::printf("[CLIENT] Nhận file '%s' <- '%s' từ %s:%u\n",
                localFile.c_str(), remoteFile.c_str(), serverIP.c_str(), pasvPort);

    // 4. Nhận dữ liệu qua RDT
    RDTReceiver receiver(udp);
    receiver.setTimeoutMs(15000);

    std::vector<uint8_t> buf;
    bool ok = receiver.receiveBuffer(buf);

    if (!ok) {
        return "Error: RDT receive failed";
    }

    // 5. Ghi file
    std::ofstream ofs(localFile, std::ios::binary);
    if (!ofs.is_open()) {
        return "Error: Cannot create local file: " + localFile;
    }
    ofs.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    ofs.close();

    // 6. Đợi reply cuối (226) từ server
    // Server đã gửi 226 sau khi transfer xong — đọc nó
    // (reply đã được đọc bởi recvReply() phía trên, không cần đọc lại)

    // 7. Tính hash để xác nhận
    std::vector<char> fileData(buf.begin(), buf.end());
    std::string hash = CryptoHash::computeSHA256(fileData);
    std::printf("[CLIENT] SHA-256 sau RETR: %s (%zu bytes)\n",
                hash.c_str(), buf.size());

    return retrReply + "\n226 Transfer complete.";
}

// ============================================================
//  STOR / STOU / APPE: upload file từ local lên server
// ============================================================
std::string ClientCLI::handleStor(const std::string& localFile,
                                   const std::string& remoteFile,
                                   bool unique, bool append)
{
    // 1. Kiểm tra file tồn tại
    std::ifstream ifs(localFile, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return "Error: Local file not found: " + localFile;
    }
    std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileBuf(static_cast<size_t>(fileSize));
    if (fileSize > 0 && !ifs.read(reinterpret_cast<char*>(fileBuf.data()), fileSize)) {
        return "Error: Cannot read local file: " + localFile;
    }
    ifs.close();

    // Hash trước khi gửi
    std::vector<char> charBuf(fileBuf.begin(), fileBuf.end());
    std::string hashBefore = CryptoHash::computeSHA256(charBuf);
    std::printf("[CLIENT] SHA-256 trước STOR: %s (%lld bytes)\n",
                hashBefore.c_str(), static_cast<long long>(fileSize));

    // 2. Gửi PASV
    std::string pasvReply = m_sendCommand("PASV");
    if (pasvReply.empty() || pasvReply.rfind("227", 0) != 0) {
        return "Error: PASV failed: " + pasvReply;
    }

    std::string serverIP;
    uint16_t pasvPort = parsePasvReply(pasvReply, serverIP);
    if (pasvPort == 0) {
        return "Error: Cannot parse PASV reply: " + pasvReply;
    }

    // 3. Gửi STOR/STOU/APPE lên server
    std::string cmd;
    if (unique) {
        cmd = remoteFile.empty() ? "STOU" : "STOU " + remoteFile;
    } else if (append) {
        cmd = "APPE " + remoteFile;
    } else {
        cmd = "STOR " + remoteFile;
    }

    std::string storReply = m_sendCommand(cmd);
    if (storReply.empty()) {
        return "Error: No reply to " + cmd;
    }
    if (storReply.rfind("150", 0) != 0 && storReply.rfind("125", 0) != 0) {
        return storReply; // lỗi
    }

    std::printf("[CLIENT] Gửi file '%s' -> '%s' đến %s:%u\n",
                localFile.c_str(), remoteFile.c_str(), serverIP.c_str(), pasvPort);

    // 4. Mở UDP socket và gửi bằng RDT
    UDPSocket udp;
    try {
        udp.open();
        udp.bind(0);
        udp.setRecvTimeout(15000);
    } catch (const std::exception& e) {
        return std::string("Error: Cannot open UDP socket: ") + e.what();
    }

    RDTSender sender(udp, serverIP, pasvPort);
    bool ok = sender.sendBuffer(fileBuf.data(), fileBuf.size());

    udp.close();

    if (!ok) {
        return "Error: RDT send failed (cwnd=" +
               std::to_string(static_cast<int>(sender.getFinalCwnd())) + ")";
    }

    std::printf("[CLIENT] Transfer OK: %lld bytes | cwnd=%.2f | timeouts=%d | segs=%u\n",
                static_cast<long long>(fileSize),
                sender.getFinalCwnd(),
                sender.getTotalTimeouts(),
                sender.getTotalSegsSent());

    return storReply + "\n226 Transfer complete.";
}

// ============================================================
//  notifyAbort() — gọi từ signal handler để yêu cầu dừng vòng lặp
// ============================================================
void ClientCLI::notifyAbort()
{
    m_aborted.store(true);
}

// ============================================================
//  run() — main REPL
// ============================================================
void ClientCLI::run()
{
    std::string line;
    while (true) {
        // Kiểm tra cờ abort (SIGINT/SIGTERM) trước mỗi lần đọc lệnh
        if (m_aborted.load()) {
            std::cout << "\n[CLIENT] Nhận tín hiệu ngắt. Đang gửi ABOR...\n";
            m_sendCommand("ABOR");
            std::cout << "[CLIENT] Đang gửi QUIT...\n";
            m_sendCommand("QUIT");
            break;
        }

        printPrompt();

        if (!std::getline(std::cin, line)) {
            // EOF (Ctrl+D) — thoát sạch
            std::cout << "\n[CLIENT] EOF — đang thoát...\n";
            if (!m_aborted.load()) {
                m_sendCommand("QUIT");
            }
            break;
        }

        line = trim(line);
        if (line.empty()) continue;

        const std::string verb = toUpperFirstWord(line);

        // --- Lệnh cần data channel (xử lý nội bộ) ---
        if (verb == "RETR" || verb == "GET") {
            // Cú pháp: RETR <remote> [local]
            std::istringstream ss(line);
            std::string w, remote, local;
            ss >> w >> remote >> local;
            if (remote.empty()) {
                std::cout << "Usage: RETR <remote_file> [local_file]\n";
                continue;
            }
            if (local.empty()) local = remote;
            std::cout << handleRetr(remote, local) << "\n";
            continue;
        }

        if (verb == "STOR" || verb == "PUT") {
            // Cú pháp: STOR <local_file> [remote_file]
            std::istringstream ss(line);
            std::string w, local, remote;
            ss >> w >> local >> remote;
            if (local.empty()) {
                std::cout << "Usage: STOR <local_file> [remote_file]\n";
                continue;
            }
            if (remote.empty()) remote = local;
            std::cout << handleStor(local, remote, false, false) << "\n";
            continue;
        }

        if (verb == "STOU") {
            std::istringstream ss(line);
            std::string w, local, remote;
            ss >> w >> local >> remote;
            if (local.empty()) {
                std::cout << "Usage: STOU <local_file> [remote_file]\n";
                continue;
            }
            if (remote.empty()) remote = local;
            std::cout << handleStor(local, remote, true, false) << "\n";
            continue;
        }

        if (verb == "APPE") {
            std::istringstream ss(line);
            std::string w, local, remote;
            ss >> w >> local >> remote;
            if (local.empty()) {
                std::cout << "Usage: APPE <local_file> [remote_file]\n";
                continue;
            }
            if (remote.empty()) remote = local;
            std::cout << handleStor(local, remote, false, true) << "\n";
            continue;
        }

        // --- Lệnh thông thường --- 
        std::string reply = m_sendCommand(line);
        std::cout << reply << "\n";

        if (verb == "QUIT") break;
    }
}

void ClientCLI::printPrompt() const {
    std::cout << "ftp> " << std::flush;
}

std::string ClientCLI::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ClientCLI::toUpperFirstWord(const std::string& s) {
    size_t spacePos = s.find(' ');
    std::string word = (spacePos == std::string::npos) ? s : s.substr(0, spacePos);
    for (char& c : word) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return word;
}
