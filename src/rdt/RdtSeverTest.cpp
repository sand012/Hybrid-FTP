#include "UDPSocket.h"
#include "ReliableTransfer.h"
#include <cstdio>

// ============================================================
//  Demo Server dung RDT (Stop-and-Wait): nhan 4 luot goi tin,
//  se co 1 luot la goi TRUNG (client co tinh gui lai) de chung
//  minh receiver loai bo dung, khong giao du lieu 2 lan.
// ============================================================
int main()
{
    const uint16_t SERVER_PORT = 9000;

    UDPSocket socket;
    socket.open();
    socket.bind(SERVER_PORT);
    printf("[SERVER] RDT lang nghe tren port %u...\n\n", SERVER_PORT);

    RDTReceiver receiver(socket);

    int packetsDelivered = 0;
    const int TOTAL_ATTEMPTS = 4; // 3 goi that + 1 goi trung (test dedup)
    for (int i = 0; i < TOTAL_ATTEMPTS; ++i)
    {
        std::vector<uint8_t> data;
        std::string senderIP;
        uint16_t senderPort;

        bool delivered = receiver.receivePacket(data, senderIP, senderPort);
        if (delivered)
        {
            packetsDelivered++;
            printf("[SERVER] >>> Du lieu hop le #%d: \"%.*s\"\n\n",
                   packetsDelivered, static_cast<int>(data.size()),
                   reinterpret_cast<const char*>(data.data()));
        }
        else
        {
            printf("[SERVER] >>> (goi nay khong duoc giao len tang tren)\n\n");
        }
    }

    printf("[SERVER] Hoan tat. Tong so goi du lieu HOP LE da giao: %d\n", packetsDelivered);
    socket.close();
    return 0;
}