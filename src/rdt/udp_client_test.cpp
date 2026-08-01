#include "UDPSocket.h"
#include <cstdio>
#include <cstring>

// ============================================================
//  Demo Client: gửi 1 gói tin thô tới server (127.0.0.1:9000),
//  chờ nhận phản hồi để xác nhận round-trip UDP hoạt động.
// ============================================================
int main()
{
    const std::string SERVER_IP = "127.0.0.1";
    const uint16_t SERVER_PORT = 9000;

    UDPSocket client;
    client.open();
    client.bind(0);
    client.setRecvTimeout(3000); // 3 giây, tránh chờ vô hạn nếu server không phản hồi

    printf("[CLIENT] Dang chay tren cong local %u\n", client.getLocalPort());

    const char *message = "Hello from UDP Client - raw packet test";
    int sent = client.sendTo(reinterpret_cast<const uint8_t *>(message), strlen(message),
                             SERVER_IP, SERVER_PORT);

    printf("[CLIENT] Da gui %d byte toi %s:%u -> \"%s\"\n",
           sent, SERVER_IP.c_str(), SERVER_PORT, message);

    uint8_t buffer[1024];
    std::string senderIP;
    uint16_t senderPort;

    int received = client.recvFrom(buffer, sizeof(buffer), senderIP, senderPort);

    if (received < 0)
    {
        printf("[CLIENT] Khong nhan duoc phan hoi (timeout hoac loi).\n");
        client.close();
        return 1;
    }

    printf("[CLIENT] Nhan %d byte phan hoi tu %s:%u -> \"%.*s\"\n",
           received, senderIP.c_str(), senderPort, received, buffer);

    client.close();
    return 0;
}