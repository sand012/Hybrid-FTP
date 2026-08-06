// ============================================================
//  FileTransferClient.cpp — Nghiệm thu RDT + Sliding Window
//
//  Sử dụng: ./rdt_file_client [server_ip] [port] [file_path]
//                             [mss] [init_cwnd] [ssthresh] [max_win]
//
//  Mặc định: 127.0.0.1  9001  test_send.bin  1024  1.0  8.0  32
//
//  Dùng RDTWindowSender: Go-Back-N + Slow Start + AIMD (TCP Reno)
//  + Flow Control (đọc advertisedWindow từ ACK của receiver).
// ============================================================
#include "UDPSocket.h"
#include "SlidingWindow.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <numeric>

// ---- Tính checksum đơn giản để kiểm tra toàn vẹn ----
static uint32_t simpleSum(const std::vector<uint8_t>& data)
{
    uint32_t sum = 0;
    for (auto b : data) sum += b;
    return sum;
}

int main(int argc, char* argv[])
{
    std::string serverIP   = "127.0.0.1";
    uint16_t    port       = 9001;
    std::string filePath   = "test_send.bin";
    size_t      mss        = 1024;
    double      initCwnd   = 1.0;
    double      ssthresh   = 8.0;
    uint32_t    maxWin     = 32;

    if (argc >= 2) serverIP  = argv[1];
    if (argc >= 3) port      = static_cast<uint16_t>(std::atoi(argv[2]));
    if (argc >= 4) filePath  = argv[3];
    if (argc >= 5) mss       = static_cast<size_t>(std::atoi(argv[4]));
    if (argc >= 6) initCwnd  = std::atof(argv[5]);
    if (argc >= 7) ssthresh  = std::atof(argv[6]);
    if (argc >= 8) maxWin    = static_cast<uint32_t>(std::atoi(argv[7]));

    // ---- Đọc file ----
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
    {
        fprintf(stderr, "[FILE-CLIENT] Khong the mo file: %s\n", filePath.c_str());
        return 1;
    }
    std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
    if (fileSize > 0 && !ifs.read(reinterpret_cast<char*>(fileData.data()), fileSize))
    {
        fprintf(stderr, "[FILE-CLIENT] Loi doc file: %s\n", filePath.c_str());
        return 1;
    }
    ifs.close();

    uint32_t checksum = simpleSum(fileData);

    printf("========================================\n");
    printf("[FILE-CLIENT] Khoi dong RDT File Sender\n");
    printf("  Server          : %s:%u\n",  serverIP.c_str(), port);
    printf("  File            : %s\n",     filePath.c_str());
    printf("  Kich thuoc      : %lld bytes\n", static_cast<long long>(fileSize));
    printf("  MSS             : %zu bytes\n", mss);
    printf("  cwnd_init       : %.1f\n",   initCwnd);
    printf("  ssthresh_init   : %.1f\n",   ssthresh);
    printf("  max_win_segs    : %u\n",     maxWin);
    printf("  SimpleSum       : 0x%08X\n", checksum);
    printf("========================================\n\n");

    UDPSocket socket;
    socket.open();
    socket.bind(0);
    printf("[FILE-CLIENT] Dang dung cong local %u\n\n", socket.getLocalPort());

    RDTWindowSender sender(socket, serverIP, port);
    sender.setMSS(mss);
    sender.setInitialCwnd(initCwnd);
    sender.setInitialSsthresh(ssthresh);
    sender.setMaxWindowSegments(maxWin);
    sender.setTimeoutMs(500);
    sender.setMaxRetransmitRounds(20);

    auto t0 = std::chrono::steady_clock::now();
    bool ok  = sender.sendData(fileData.data(), fileData.size());
    auto t1  = std::chrono::steady_clock::now();

    double elapsed    = std::chrono::duration<double>(t1 - t0).count();
    double throughput = (ok && elapsed > 0)
                        ? (fileData.size() / elapsed / 1024.0)
                        : 0.0;

    printf("\n========================================\n");
    printf("[FILE-CLIENT] KET QUA NGHIEM THU\n");
    printf("  Trang thai          : %s\n",   ok ? "THANH CONG" : "THAT BAI");
    printf("  Tong bytes gui      : %zu\n",   fileData.size());
    printf("  Thoi gian           : %.3f giay\n", elapsed);
    printf("  Thong luong         : %.1f KB/s\n", throughput);
    printf("  cwnd cuoi           : %.2f\n",  sender.getFinalCwnd());
    printf("  So timeout          : %d\n",    sender.getTotalTimeouts());
    printf("  Tong segment da gui : %u\n",    sender.getTotalPacketsSent());
    printf("  SimpleSum           : 0x%08X\n", checksum);
    printf("========================================\n");

    socket.close();
    return ok ? 0 : 1;
}
