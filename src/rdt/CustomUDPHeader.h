#pragma once
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)
struct CustomUDPHeader
{
    uint32_t seqNum;
    uint32_t ackNum;
    uint16_t payloadLen;
    uint16_t windowSize;
    uint16_t checksum;
    uint8_t flags;
};

#pragma pack(pop)

// header size
constexpr size_t CUSTOM_UDP_HEADER_SIZE = sizeof(CustomUDPHeader);

// define bit in "flags"

enum RDTFlags : uint8_t
{
    FLAG_SYN = 1 << 0,
    FLAG_ACK = 1 << 1,
    FLAG_DATA = 1 << 2,
    FLAG_FIN = 1 << 3,
    FLAG_NAK = 1 << 4,
};

inline void setFlag(CustomUDPHeader &hdr, RDTFlags flag)
{
    hdr.flags |= static_cast<uint8_t>(flag);
}

inline void clearFlag(CustomUDPHeader &hdr, RDTFlags flag)
{
    hdr.flags &= static_cast<uint8_t>(~flag);
}

inline bool hasFlag(const CustomUDPHeader &hdr, RDTFlags flag)
{
    return (hdr.flags & static_cast<uint8_t>(flag)) != 0;
}

uint16_t calculateChecksum(const CustomUDPHeader &hdr,
                           const uint8_t *payload,
                           size_t payloadLen);

bool verifyChecksum(const CustomUDPHeader &hdr,
                    const uint8_t *payload,
                    size_t payloadLen);

void serializeHeader(const CustomUDPHeader &hdr, uint8_t *buffer);

CustomUDPHeader deserializeHeader(const uint8_t *buffer);