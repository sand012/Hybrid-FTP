#include "CustomUDPHeader.h"
#include <cstring>
#include <vector>

#if defined(_WIN32)
    #include <winsock2.h>   // htonl, htons, ntohl, ntohs (cần link Ws2_32.lib)
#else
    #include <arpa/inet.h>  // htonl, htons, ntohl, ntohs (Linux/macOS)
#endif

// ============================================================
//  Checksum — Internet Checksum (giống nguyên lý TCP/UDP chuẩn)
//  Tính trên: [header với checksum = 0] + [payload]
// ============================================================
static uint16_t internetChecksum(const uint8_t* data, size_t len)
{
    uint32_t sum = 0;

    // Cộng dồn từng cặp 2 byte (16-bit word)
    for (size_t i = 0; i + 1 < len; i += 2)
    {
        uint16_t word = (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
        sum += word;
        if (sum & 0xFFFF0000)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);  // xử lý carry
        }
    }

    // Nếu độ dài lẻ, byte cuối cùng đứng riêng
    if (len % 2 == 1)
    {
        uint16_t word = static_cast<uint16_t>(data[len - 1]) << 8;
        sum += word;
        if (sum & 0xFFFF0000)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
    }

    return static_cast<uint16_t>(~sum & 0xFFFF);
}

uint16_t calculateChecksum(const CustomUDPHeader& hdr,
                            const uint8_t* payload,
                            size_t payloadLen)
{
    // Tạo bản sao header, set checksum = 0 trước khi tính
    // (tránh việc checksum tự tham chiếu chính nó)
    CustomUDPHeader tmp = hdr;
    tmp.checksum = 0;

    uint8_t headerBuf[CUSTOM_UDP_HEADER_SIZE];
    serializeHeader(tmp, headerBuf);

    // Gộp header + payload vào 1 buffer liên tục để tính checksum
    std::vector<uint8_t> combined(CUSTOM_UDP_HEADER_SIZE + payloadLen);
    std::memcpy(combined.data(), headerBuf, CUSTOM_UDP_HEADER_SIZE);
    if (payload != nullptr && payloadLen > 0)
    {
        std::memcpy(combined.data() + CUSTOM_UDP_HEADER_SIZE, payload, payloadLen);
    }

    return internetChecksum(combined.data(), combined.size());
}

bool verifyChecksum(const CustomUDPHeader& hdr,
                     const uint8_t* payload,
                     size_t payloadLen)
{
    uint16_t received = hdr.checksum;
    uint16_t computed  = calculateChecksum(hdr, payload, payloadLen);
    return received == computed;
}

// ============================================================
//  Serialize / Deserialize — chuyển đổi sang network byte order
//  để đảm bảo tương thích khi client/server chạy trên kiến trúc
//  endianness khác nhau.
// ============================================================
void serializeHeader(const CustomUDPHeader& hdr, uint8_t* buffer)
{
    uint32_t netSeq  = htonl(hdr.seqNum);
    uint32_t netAck  = htonl(hdr.ackNum);
    uint16_t netLen  = htons(hdr.payloadLen);
    uint16_t netWin  = htons(hdr.windowSize);
    uint16_t netCsum = htons(hdr.checksum);

    size_t offset = 0;
    std::memcpy(buffer + offset, &netSeq, sizeof(netSeq));  offset += sizeof(netSeq);
    std::memcpy(buffer + offset, &netAck, sizeof(netAck));  offset += sizeof(netAck);
    std::memcpy(buffer + offset, &netLen, sizeof(netLen));  offset += sizeof(netLen);
    std::memcpy(buffer + offset, &netWin, sizeof(netWin));  offset += sizeof(netWin);
    std::memcpy(buffer + offset, &netCsum, sizeof(netCsum)); offset += sizeof(netCsum);
    std::memcpy(buffer + offset, &hdr.flags, sizeof(hdr.flags));
}

CustomUDPHeader deserializeHeader(const uint8_t* buffer)
{
    CustomUDPHeader hdr{};
    size_t offset = 0;

    uint32_t netSeq, netAck;
    uint16_t netLen, netWin, netCsum;

    std::memcpy(&netSeq, buffer + offset, sizeof(netSeq));  offset += sizeof(netSeq);
    std::memcpy(&netAck, buffer + offset, sizeof(netAck));  offset += sizeof(netAck);
    std::memcpy(&netLen, buffer + offset, sizeof(netLen));  offset += sizeof(netLen);
    std::memcpy(&netWin, buffer + offset, sizeof(netWin));  offset += sizeof(netWin);
    std::memcpy(&netCsum, buffer + offset, sizeof(netCsum)); offset += sizeof(netCsum);
    std::memcpy(&hdr.flags, buffer + offset, sizeof(hdr.flags));

    hdr.seqNum     = ntohl(netSeq);
    hdr.ackNum     = ntohl(netAck);
    hdr.payloadLen = ntohs(netLen);
    hdr.windowSize = ntohs(netWin);
    hdr.checksum   = ntohs(netCsum);

    return hdr;
}