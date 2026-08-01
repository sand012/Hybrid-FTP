#include "UDPSocket.h"
#include <cstdio>
#include <cstring>

// ============================================================
//  Demo Server: lắng nghe port 9000, nhận gói tin thô,
//  in ra nội dung + địa chỉ người gửi, rồi gửi lại phản hồi ACK.
// ============================================================
int main()
{
    const uint16_t SERVER_PORT = 9000;

    UDPSocket server;
    server.open();
    server.bind(SERVER_PORT);

    printf("[SERVER] Dang lang nghe tren port %u...\n", SERVER_PORT);

    uint8_t buffer[1024];
    std::string senderIP;
    uint16_t senderPort;

    int received = server.recvFrom(buffer, sizeof(buffer), senderIP, senderPort);

    if (received < 0)
    {
        printf("[SERVER] Loi khi nhan goi tin.\n");
        return 1;
    }

    // In ra nội dung raw nhận được (giả định là text để dễ kiểm tra)
    printf("[SERVER] Nhan %d byte tu %s:%u -> \"%.*s\"\n",
           received, senderIP.c_str(), senderPort, received, buffer);

    // Gửi phản hồi lại cho client để xác nhận round-trip thành công
    const char* reply = "ACK: Server da nhan goi tin thanh cong";
    int sent = server.sendTo(reinterpret_cast<const uint8_t*>(reply), strlen(reply),
                              senderIP, senderPort);

    printf("[SERVER] Da gui phan hoi (%d byte) ve cho client.\n", sent);

    server.close();
    return 0;
}