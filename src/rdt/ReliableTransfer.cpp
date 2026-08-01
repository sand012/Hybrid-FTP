#include "ReliableTransfer.h"
#include <cstdio>
#include <cstring>

RDTSender::RDTSender(UDPSocket& socket, const std::string& destIP, uint16_t destPort)
    : socket_(socket), destIP_(destIP), destPort_(destPort),
      currentSeq_(0), state_(SenderState::WAIT_CALL_0)
{
}

bool RDTSender::sendPacket(const uint8_t* data, size_t len)
{
   
    CustomUDPHeader hdr{};
    hdr.seqNum = currentSeq_;
    hdr.ackNum = 0;
    hdr.payloadLen = static_cast<uint16_t>(len);
    hdr.windowSize = 1; 
    setFlag(hdr, FLAG_DATA);
    hdr.checksum = calculateChecksum(hdr, data, len);

    std::vector<uint8_t> buffer(CUSTOM_UDP_HEADER_SIZE + len);
    serializeHeader(hdr, buffer.data());
    if (len > 0)
    {
        std::memcpy(buffer.data() + CUSTOM_UDP_HEADER_SIZE, data, len);
    }

    // Chuyển state: đã gửi, giờ đang chờ ACK tương ứng
    state_ = (currentSeq_ == 0) ? SenderState::WAIT_ACK_0 : SenderState::WAIT_ACK_1;

    int sent = socket_.sendTo(buffer.data(), buffer.size(), destIP_, destPort_);
    if (sent < 0)
    {
        printf("[RDT-SENDER] Loi khi gui goi tin (seq=%u)\n", currentSeq_);
        return false;
    }
    printf("[RDT-SENDER] Da gui goi seq=%u (%zu byte payload)\n", currentSeq_, len);

    // ---- Bước 2: Chờ ACK 
    uint8_t recvBuf[2048];
    std::string ackFromIP;
    uint16_t ackFromPort;

    int received = socket_.recvFrom(recvBuf, sizeof(recvBuf), ackFromIP, ackFromPort);
    if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
    {
        printf("[RDT-SENDER] Khong nhan duoc ACK (timeout hoac loi).\n");
        return false;
    }

    CustomUDPHeader ackHdr = deserializeHeader(recvBuf);
    const uint8_t* ackPayload = recvBuf + CUSTOM_UDP_HEADER_SIZE;
    size_t ackPayloadLen = static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE;

    bool checksumOk = verifyChecksum(ackHdr, ackPayload, ackPayloadLen);
    bool isAck = hasFlag(ackHdr, FLAG_ACK);
    bool correctSeq = (ackHdr.ackNum == currentSeq_);

    if (checksumOk && isAck && correctSeq)
    {
        printf("[RDT-SENDER] Nhan dung ACK(%u) -> thanh cong.\n", ackHdr.ackNum);
        // Chuyển bit luân phiên + state cho lần gửi tiếp theo
        currentSeq_ = 1 - currentSeq_;
        state_ = (currentSeq_ == 0) ? SenderState::WAIT_CALL_0 : SenderState::WAIT_CALL_1;
        return true;
    }

    printf("[RDT-SENDER] ACK khong hop le (checksumOk=%d, isAck=%d, ackNum=%u, mong doi=%u)\n",
           checksumOk, isAck, ackHdr.ackNum, currentSeq_);
    return false;
}

//  RDTReceiver
RDTReceiver::RDTReceiver(UDPSocket& socket)
    : socket_(socket), state_(ReceiverState::WAIT_SEQ_0),
      expectedSeq_(0), lastAckSent_(1) 
{
}

bool RDTReceiver::receivePacket(std::vector<uint8_t>& outData,
                                 std::string& senderIP, uint16_t& senderPort)
{
    uint8_t recvBuf[2048];
    int received = socket_.recvFrom(recvBuf, sizeof(recvBuf), senderIP, senderPort);

    if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
    {
        printf("[RDT-RECEIVER] Goi tin qua nho hoac loi nhan.\n");
        return false;
    }

    CustomUDPHeader hdr = deserializeHeader(recvBuf);
    const uint8_t* payload = recvBuf + CUSTOM_UDP_HEADER_SIZE;
    size_t payloadLen = static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE;

    // ---- Kiem tra checksum
    if (!verifyChecksum(hdr, payload, payloadLen))
    {
        printf("[RDT-RECEIVER] Checksum SAI (seq=%u) -> bo qua goi tin, khong gui ACK.\n",
               hdr.seqNum);
        return false;
    }

    // ---- Truong hop 1: Goi MOI dung thu tu dang mong doi ----
    if (hdr.seqNum == expectedSeq_)
    {
        outData.assign(payload, payload + payloadLen);

        // Gui ACK cho goi vua nhan
        CustomUDPHeader ackHdr{};
        ackHdr.seqNum = 0;
        ackHdr.ackNum = hdr.seqNum;
        ackHdr.payloadLen = 0;
        ackHdr.windowSize = 1;
        setFlag(ackHdr, FLAG_ACK);
        ackHdr.checksum = calculateChecksum(ackHdr, nullptr, 0);

        uint8_t ackBuf[CUSTOM_UDP_HEADER_SIZE];
        serializeHeader(ackHdr, ackBuf);
        socket_.sendTo(ackBuf, sizeof(ackBuf), senderIP, senderPort);

        printf("[RDT-RECEIVER] Nhan goi MOI seq=%u (%zu byte) -> da gui ACK(%u)\n",
               hdr.seqNum, payloadLen, hdr.seqNum);

        lastAckSent_ = hdr.seqNum;
        expectedSeq_ = 1 - expectedSeq_;
        state_ = (expectedSeq_ == 0) ? ReceiverState::WAIT_SEQ_0 : ReceiverState::WAIT_SEQ_1;
        return true;
    }

    // ---- Truong hop 2: Goi TRUNG (da nhan roi, do ACK truoc bi mat) ----
    printf("[RDT-RECEIVER] Nhan goi TRUNG seq=%u (mong doi=%u) -> gui lai ACK(%u), khong giao du lieu.\n",
           hdr.seqNum, expectedSeq_, lastAckSent_);

    CustomUDPHeader dupAckHdr{};
    dupAckHdr.seqNum = 0;
    dupAckHdr.ackNum = lastAckSent_;
    dupAckHdr.payloadLen = 0;
    dupAckHdr.windowSize = 1;
    setFlag(dupAckHdr, FLAG_ACK);
    dupAckHdr.checksum = calculateChecksum(dupAckHdr, nullptr, 0);

    uint8_t dupAckBuf[CUSTOM_UDP_HEADER_SIZE];
    serializeHeader(dupAckHdr, dupAckBuf);
    socket_.sendTo(dupAckBuf, sizeof(dupAckBuf), senderIP, senderPort);

    return false;
}