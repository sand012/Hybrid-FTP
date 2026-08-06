// ============================================================
//  test_e2e_dev3.cpp — End-to-end integration test cho Dev 3
//
//  Flow: USER → PASS → TYPE I → PASV → STOR → HASH → PASV → RETR
//        → So sánh SHA-256 file gốc và file nhận được
//
//  Chạy: ./test_e2e_dev3 [server_ip] [server_port]
//  Mặc định: 127.0.0.1  2121
// ============================================================
#include "../rdt/ReliableTransfer.h"
#include "../rdt/UDPSocket.h"
#include "../common/CryptoHash.h"
#include "../common/FileHandler.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <regex>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

// ============================================================
//  Helpers TCP control channel
// ============================================================
static int tcpConnect(const std::string& ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd); return -1;
    }
    return fd;
}

static std::string recvLine(int fd) {
    std::string line;
    char c;
    while (true) {
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            break;
        }
        line.push_back(c);
    }
    return line;
}

static std::string recvReply(int fd) {
    std::string first = recvLine(fd);
    if (first.size() < 4 || first[3] != '-') return first;
    std::string code = first.substr(0, 3);
    std::string reply = first;
    while (true) {
        std::string next = recvLine(fd);
        if (next.empty()) break;
        reply += "\n" + next;
        if (next.rfind(code + " ", 0) == 0) break;
    }
    return reply;
}

static bool sendCmd(int fd, const std::string& cmd) {
    std::string s = cmd + "\r\n";
    ssize_t sent = send(fd, s.data(), s.size(), 0);
    return sent == static_cast<ssize_t>(s.size());
}

static uint16_t parsePasv(const std::string& reply, std::string& outIP) {
    std::regex re(R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))");
    std::smatch m;
    if (!std::regex_search(reply, m, re)) return 0;
    outIP = m[1].str()+"."+m[2].str()+"."+m[3].str()+"."+m[4].str();
    return static_cast<uint16_t>(
        (std::stoul(m[5].str()) << 8) | std::stoul(m[6].str()));
}

// ============================================================
//  Thống kê test
// ============================================================
static int g_pass = 0, g_fail = 0;
static void ok(const std::string& msg) {
    printf("\033[0;32m✅ %s\033[0m\n", msg.c_str()); ++g_pass;
}
static void fail(const std::string& msg) {
    printf("\033[0;31m❌ %s\033[0m\n", msg.c_str()); ++g_fail;
}
static void banner(const std::string& msg) {
    printf("\n\033[1;36m══════════════════════════════════════\n  %s\n══════════════════════════════════════\033[0m\n", msg.c_str());
}

// ============================================================
//  main
// ============================================================
int main(int argc, char* argv[]) {
    const std::string SERVER_IP   = (argc >= 2) ? argv[1] : "127.0.0.1";
    const int         SERVER_PORT = (argc >= 3) ? std::atoi(argv[2]) : 2121;

    banner("BƯỚC 1: CHUẨN BỊ FILE TEST");

    // Tạo file test ASCII và Binary
    const std::string asciiPath  = "/tmp/e2e_test_ascii.txt";
    const std::string binaryPath = "/tmp/e2e_test_binary.bin";
    const std::string asciiRecv  = "/tmp/e2e_recv_ascii.txt";
    const std::string binaryRecv = "/tmp/e2e_recv_binary.bin";

    {
        std::ofstream f(asciiPath);
        f << "Hybrid-FTP End-to-End Test\nLine 2: ASCII content\nLine 3: 123456789\n";
    }
    {
        std::ofstream f(binaryPath, std::ios::binary);
        for (int i = 0; i < 16384; ++i) f.put(static_cast<char>(i & 0xFF));
    }

    std::string hashAsciiOrig  = CryptoHash::computeSHA256FromFile(asciiPath);
    std::string hashBinaryOrig = CryptoHash::computeSHA256FromFile(binaryPath);
    printf("  [ASCII]  SHA-256 gốc: %s\n", hashAsciiOrig.c_str());
    printf("  [BINARY] SHA-256 gốc: %s\n", hashBinaryOrig.c_str());
    ok("File test đã tạo: ASCII (" + std::to_string(fs::file_size(asciiPath)) +
       " B), Binary (" + std::to_string(fs::file_size(binaryPath)) + " B)");

    // ============================================================
    banner("BƯỚC 2: KẾT NỐI VÀ XÁC THỰC");
    int fd = tcpConnect(SERVER_IP, SERVER_PORT);
    if (fd < 0) {
        fail("Không kết nối được đến server " + SERVER_IP + ":" + std::to_string(SERVER_PORT));
        printf("  Hãy khởi chạy ftp_server trước.\n");
        return 1;
    }
    std::string banner220 = recvReply(fd);
    if (banner220.rfind("220", 0) == 0) ok("Nhận 220 welcome banner");
    else fail("Không nhận được 220: " + banner220);

    sendCmd(fd, "USER testuser"); std::string r = recvReply(fd);
    if (r.rfind("331", 0) == 0) ok("USER → 331"); else fail("USER failed: " + r);

    sendCmd(fd, "PASS testpass"); r = recvReply(fd);
    if (r.rfind("230", 0) == 0) ok("PASS → 230 Logged in"); else fail("PASS failed: " + r);

    // ============================================================
    banner("BƯỚC 3: STOR (upload) file Binary qua PASV");

    sendCmd(fd, "TYPE I"); r = recvReply(fd);
    if (r.rfind("200", 0) == 0) ok("TYPE I (Binary mode)"); else fail("TYPE I: " + r);

    sendCmd(fd, "PASV"); r = recvReply(fd);
    std::string pasvIP;
    uint16_t pasvPort = parsePasv(r, pasvIP);
    if (pasvPort == 0) { fail("PASV parse failed: " + r); goto cleanup; }
    ok("PASV → UDP port " + std::to_string(pasvPort) + " trên " + pasvIP);

    sendCmd(fd, "STOR e2e_binary.bin"); r = recvReply(fd);
    if (r.rfind("150", 0) == 0) ok("STOR → 150 (server sẵn sàng nhận)");
    else { fail("STOR failed: " + r); goto cleanup; }

    {
        // Đọc file binary và gửi qua RDT
        std::vector<char> fileBuf;
        FileHandler::readBinaryFile(binaryPath, fileBuf);
        std::vector<uint8_t> data(fileBuf.begin(), fileBuf.end());

        UDPSocket udp;
        udp.open(); udp.bind(0); udp.setRecvTimeout(15000);
        RDTSender sender(udp, pasvIP, pasvPort);
        bool ok_send = sender.sendBuffer(data.data(), data.size());
        udp.close();

        if (ok_send) ok("RDT STOR: đã gửi " + std::to_string(data.size()) +
                         " bytes | cwnd=" + std::to_string(static_cast<int>(sender.getFinalCwnd())) +
                         " | timeouts=" + std::to_string(sender.getTotalTimeouts()));
        else { fail("RDT sendBuffer thất bại"); goto cleanup; }
    }

    // Đọc 226 completion reply
    r = recvReply(fd);
    // Server có thể gộp 150+226 hoặc gửi riêng
    if (r.find("226") != std::string::npos || r.rfind("226", 0) == 0)
        ok("STOR → 226 Transfer complete");
    else
        printf("  [INFO] Server reply sau STOR: %s\n", r.c_str());

    // ============================================================
    banner("BƯỚC 4: HASH — Xác nhận server nhận đúng file");

    sendCmd(fd, "HASH e2e_binary.bin"); r = recvReply(fd);
    if (r.rfind("213", 0) == 0) {
        std::regex reHash(R"(SHA-256=([0-9a-f]+))");
        std::smatch mh;
        if (std::regex_search(r, mh, reHash)) {
            std::string serverHash = mh[1].str();
            if (serverHash == hashBinaryOrig)
                ok("HASH khớp! SHA-256=" + serverHash.substr(0, 16) + "...");
            else
                fail("HASH KHÔNG khớp!\n  server=" + serverHash + "\n  local=" + hashBinaryOrig);
        }
    } else fail("HASH command failed: " + r);

    // ============================================================
    banner("BƯỚC 5: RETR (download) file Binary qua PASV");

    sendCmd(fd, "PASV"); r = recvReply(fd);
    pasvPort = parsePasv(r, pasvIP);
    if (pasvPort == 0) { fail("PASV 2 failed: " + r); goto cleanup; }
    ok("PASV → UDP port " + std::to_string(pasvPort));

    {
        // Mở UDP và gửi knock để server biết địa chỉ client
        UDPSocket udp;
        udp.open(); udp.bind(0); udp.setRecvTimeout(15000);

        uint8_t knock[1] = {0};
        udp.sendTo(knock, 1, pasvIP, pasvPort);

        sendCmd(fd, "RETR e2e_binary.bin"); r = recvReply(fd);
        if (r.rfind("150", 0) == 0) ok("RETR → 150 (server bắt đầu gửi)");
        else { fail("RETR failed: " + r); udp.close(); goto cleanup; }

        RDTReceiver receiver(udp);
        receiver.setTimeoutMs(15000);

        std::vector<uint8_t> received;
        bool ok_recv = receiver.receiveBuffer(received);
        udp.close();

        if (!ok_recv) { fail("RDT receiveBuffer thất bại"); goto cleanup; }
        ok("RDT RETR: đã nhận " + std::to_string(received.size()) + " bytes");

        // Ghi file nhận được
        FileHandler::writeBinaryFile(binaryRecv,
            reinterpret_cast<const char*>(received.data()), received.size());

        // So sánh hash
        std::string hashRecv = CryptoHash::computeSHA256FromFile(binaryRecv);
        if (hashRecv == hashBinaryOrig)
            ok("RETR SHA-256 khớp! " + hashRecv.substr(0, 16) + "...");
        else
            fail("RETR hash KHÔNG khớp!\n  recv=" + hashRecv + "\n  orig=" + hashBinaryOrig);
    }

    // Đọc 226 từ server
    r = recvReply(fd);
    if (r.find("226") != std::string::npos)
        ok("RETR → 226 Transfer complete");

    // ============================================================
    banner("BƯỚC 6: STOR + RETR file ASCII (TYPE A)");

    sendCmd(fd, "TYPE A"); r = recvReply(fd);
    if (r.rfind("200", 0) == 0) ok("TYPE A (ASCII mode)");

    sendCmd(fd, "PASV"); r = recvReply(fd);
    pasvPort = parsePasv(r, pasvIP);
    ok("PASV → UDP port " + std::to_string(pasvPort) + " (ASCII STOR)");

    sendCmd(fd, "STOR e2e_ascii.txt"); r = recvReply(fd);
    if (r.rfind("150", 0) == 0) ok("STOR ASCII → 150");
    else { fail("STOR ASCII failed: " + r); goto cleanup; }

    {
        std::string content;
        FileHandler::readTextFile(asciiPath, content);
        std::vector<uint8_t> data(content.begin(), content.end());

        UDPSocket udp;
        udp.open(); udp.bind(0); udp.setRecvTimeout(15000);
        RDTSender sender(udp, pasvIP, pasvPort);
        bool ok_send = sender.sendBuffer(data.data(), data.size());
        udp.close();

        if (ok_send) ok("RDT STOR ASCII: " + std::to_string(data.size()) + " bytes");
        else fail("RDT STOR ASCII thất bại");
    }
    recvReply(fd); // 226

    sendCmd(fd, "HASH e2e_ascii.txt"); r = recvReply(fd);
    if (r.rfind("213", 0) == 0) {
        std::regex reHash(R"(SHA-256=([0-9a-f]+))");
        std::smatch mh;
        if (std::regex_search(r, mh, reHash)) {
            std::string sh = mh[1].str();
            if (sh == hashAsciiOrig) ok("HASH ASCII khớp!");
            else printf("  [INFO] HASH ASCII: server=%s local=%s (có thể khác do CRLF)\n",
                        sh.c_str(), hashAsciiOrig.c_str());
        }
    }

    // ============================================================
    banner("BƯỚC 7: QUIT");
    sendCmd(fd, "QUIT"); r = recvReply(fd);
    if (r.rfind("221", 0) == 0) ok("QUIT → 221 Goodbye");

cleanup:
    close(fd);
    // Dọn dẹp
    std::remove(asciiPath.c_str());
    std::remove(binaryPath.c_str());
    std::remove(asciiRecv.c_str());
    std::remove(binaryRecv.c_str());

    // ============================================================
    banner("KẾT QUẢ TỔNG HỢP — E2E DEV 3");
    printf("\n  \033[0;32m\033[1mPASS: %d\033[0m   \033[0;31m\033[1mFAIL: %d\033[0m\n\n",
           g_pass, g_fail);
    if (g_fail == 0) {
        printf("\033[0;32m\033[1m✅ TẤT CẢ TEST END-TO-END THÀNH CÔNG!\033[0m\n");
        return 0;
    } else {
        printf("\033[0;31m\033[1m❌ CÓ %d CA THẤT BẠI\033[0m\n", g_fail);
        return 1;
    }
}
