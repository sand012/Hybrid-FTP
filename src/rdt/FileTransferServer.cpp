// ============================================================
//  FileTransferServer.cpp — Nghiệm thu RDT + Sliding Window
//
//  Sử dụng: ./rdt_file_server [port] [output_file] [adv_window]
//    Mặc định: 9001  received_output.bin  8
//
//  Dùng RDTWindowReceiver: GBN Cumulative ACK +
//  advertisedWindow cho Flow Control (báo ngược sender biết
//  số slot còn trống, sender điều chỉnh cwnd & recvWindow_).
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

// ---- Tính checksum đơn giản để so sánh với sender ----
static uint32_t simpleSum(const std::vector<uint8_t>& data)
{
    uint32_t sum = 0;
    for (auto b : data) sum += b;
    return sum;
}

int main(int argc, char* argv[])
{
    uint16_t    port       = 9001;
    std::string outputPath = "received_output.bin";
    uint16_t    advWindow  = 8;

    if (argc >= 2) port       = static_cast<uint16_t>(std::atoi(argv[1]));
    if (argc >= 3) outputPath = argv[2];
    if (argc >= 4) advWindow  = static_cast<uint16_t>(std::atoi(argv[3]));

    printf("========================================\n");
    printf("[FILE-SERVER] Khoi dong RDT File Receiver\n");
    printf("  Port             : %u\n", port);
    printf("  Output           : %s\n", outputPath.c_str());
    printf("  Advertised Window: %u segments\n", advWindow);
    printf("========================================\n\n");

    UDPSocket socket;
    socket.open();
    socket.bind(port);

    // Timeout rộng để đợi sender khởi động
    socket.setRecvTimeout(8000);

    RDTWindowReceiver receiver(socket, advWindow);

    printf("[FILE-SERVER] Dang cho nhan du lieu...\n");

    auto t0 = std::chrono::steady_clock::now();

    std::vector<uint8_t> buffer;
    std::string senderIP;
    uint16_t    senderPort = 0;
    bool ok = receiver.receiveData(buffer, senderIP, senderPort);

    auto t1 = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    if (!ok)
    {
        fprintf(stderr, "[FILE-SERVER] Loi khi nhan du lieu!\n");
        socket.close();
        return 1;
    }

    // Ghi ra file
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open())
    {
        fprintf(stderr, "[FILE-SERVER] Khong the ghi file: %s\n", outputPath.c_str());
        socket.close();
        return 1;
    }
    ofs.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    ofs.close();

    double throughput = (elapsed > 0) ? (buffer.size() / elapsed / 1024.0) : 0.0;

    printf("\n========================================\n");
    printf("[FILE-SERVER] KET QUA NGHIEM THU\n");
    printf("  Sender           : %s:%u\n",    senderIP.c_str(), senderPort);
    printf("  Tong bytes nhan  : %zu\n",      buffer.size());
    printf("  Thoi gian        : %.3f giay\n", elapsed);
    printf("  Thong luong      : %.1f KB/s\n", throughput);
    printf("  SimpleSum        : 0x%08X\n",   simpleSum(buffer));
    printf("  File ghi ra      : %s\n",       outputPath.c_str());
    printf("========================================\n");

    socket.close();
    return 0;
}
