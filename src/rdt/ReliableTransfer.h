#pragma once
#include "UDPSocket.h"
#include "CustomUDPHeader.h"
#include <string>
#include <vector>
#include <cstdint>

enum class SenderState
{
    WAIT_CALL_0,   
    WAIT_ACK_0,    
    WAIT_CALL_1,   
    WAIT_ACK_1     
};

//  Trạng thái phía Receiver
enum class ReceiverState
{
    WAIT_SEQ_0,    // Đang chờ nhận gói có seqNum = 0
    WAIT_SEQ_1     // Đang chờ nhận gói có seqNum = 1
};

//  RDTSender — gửi dữ liệu tin cậy theo Stop-and-Wait
class RDTSender
{
public:
    RDTSender(UDPSocket& socket, const std::string& destIP, uint16_t destPort);

    bool sendPacket(const uint8_t* data, size_t len);

    SenderState getState() const { return state_; }

private:
    UDPSocket& socket_;
    std::string destIP_;
    uint16_t destPort_;
    uint32_t currentSeq_;   // 0 hoặc 1 — bit luân phiên hiện tại
    SenderState state_;
};

//  RDTReceiver — nhận dữ liệu tin cậy, tự loại bỏ gói trùng
class RDTReceiver
{
public:
    explicit RDTReceiver(UDPSocket& socket);

    bool receivePacket(std::vector<uint8_t>& outData,
                        std::string& senderIP, uint16_t& senderPort);

    ReceiverState getState() const { return state_; }

private:
    UDPSocket& socket_;
    ReceiverState state_;
    uint32_t expectedSeq_;   // seqNum đang mong đợi (0 hoặc 1)
    uint32_t lastAckSent_;   // ackNum của lần ACK gần nhất (dùng khi phải gửi lại ACK cho gói trùng)
};