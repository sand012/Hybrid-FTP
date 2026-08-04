#include "UDPSocket.h"
#include "ReliableTransfer.h"
#include <cstdio>
#include <cstring>

// ============================================================
//  Demo Client dung RDT (Stop-and-Wait): gui 3 goi du lieu that,
//  sau do CO TINH gui lai goi dau tien (gia lap truong hop
//  ACK bi that lac tren mang that / hoac sender gui du) de
//  server chung minh no phat hien goi TRUNG va khong giao lai.
// ============================================================
int main()
{
    const std::string SERVER_IP = "127.0.0.1";
    const uint16_t SERVER_PORT = 9000;

    UDPSocket socket;
    socket.open();
    socket.bind(0);
    socket.setRecvTimeout(3000);

    printf("[CLIENT] RDT dang chay tren cong local %u\n\n", socket.getLocalPort());

    RDTSender sender(socket, SERVER_IP, SERVER_PORT);

    const char* messages[] = {
        "Goi du lieu so 1",
        "Goi du lieu so 2",
        "Goi du lieu so 3"
    };

    // ---- Gui 3 goi that su, tuan tu qua Stop-and-Wait ----
    for (int i = 0; i < 3; ++i)
    {
        bool ok = sender.sendPacket(reinterpret_cast<const uint8_t*>(messages[i]),
                                     strlen(messages[i]));
        printf("[CLIENT] Ket qua goi #%d: %s\n\n", i + 1, ok ? "THANH CONG" : "THAT BAI");
    }

    // ---- Gia lap 1 goi TRUNG: gui lai dung noi dung + seqNum cua goi dau tien ----
    // (RDTSender da tu dong tang seqNum sau moi lan gui thanh cong, nen o day
    //  minh gui thu cong 1 goi header seq=0 "gia" de mo phong tinh huong
    //  ACK bi that lac khien sender phai gui lai goi cu.)
    printf("[CLIENT] >>> Mo phong gui LAI goi dau tien (seq=0) do ACK bi that lac...\n");

    CustomUDPHeader dupHdr{};
    dupHdr.seqNum = 0; // co tinh dung lai seq=0 (trung voi goi dau tien da ACK)
    dupHdr.ackNum = 0;
    dupHdr.payloadLen = static_cast<uint16_t>(strlen(messages[0]));
    dupHdr.windowSize = 1;
    setFlag(dupHdr, FLAG_DATA);
    dupHdr.checksum = calculateChecksum(dupHdr,
                                         reinterpret_cast<const uint8_t*>(messages[0]),
                                         dupHdr.payloadLen);

    std::vector<uint8_t> dupBuffer(CUSTOM_UDP_HEADER_SIZE + dupHdr.payloadLen);
    serializeHeader(dupHdr, dupBuffer.data());
    memcpy(dupBuffer.data() + CUSTOM_UDP_HEADER_SIZE, messages[0], dupHdr.payloadLen);

    socket.sendTo(dupBuffer.data(), dupBuffer.size(), SERVER_IP, SERVER_PORT);

    // Nhan ACK cho goi trung nay (server se gui lai ACK cu, khong giao du lieu)
    uint8_t ackBuf[64];
    std::string ip; uint16_t port;
    socket.recvFrom(ackBuf, sizeof(ackBuf), ip, port);
    printf("[CLIENT] Da nhan phan hoi cho goi TRUNG (server se khong giao du lieu nay).\n");

    socket.close();
    return 0;
}