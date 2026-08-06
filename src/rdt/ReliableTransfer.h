#pragma once
#include "UDPSocket.h"
#include "CustomUDPHeader.h"
#include "SlidingWindow.h"   // RDTWindowSender / RDTWindowReceiver
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
//  Hằng dùng chung (backward-compat với code cũ)
// ============================================================
constexpr size_t RDT_CHUNK_SIZE = 1024; // MSS mặc định (byte) — khớp SlidingWindow MSS

// ============================================================
//  RDTSender
//  Lớp bọc hướng đến Dev3/Session: cung cấp sendFile(), sendBuffer()
//  (dùng RDTWindowSender bên trong) và sendPacket() Stop-and-Wait
//  cho backward-compat với demo giai đoạn trước.
// ============================================================
class RDTSender
{
public:
    RDTSender(UDPSocket& socket, const std::string& destIP, uint16_t destPort);

    // ---- API chính (dùng Sliding Window + Flow/Congestion Control) ----

    // Đọc toàn bộ file từ filePath, gửi tin cậy qua UDP + GBN + AIMD.
    bool sendFile(const std::string& filePath);

    // Gửi buffer thô (khi dữ liệu đã có sẵn trong RAM).
    bool sendBuffer(const uint8_t* data, size_t totalLen);

    // ---- Backward-compat: Stop-and-Wait đơn gói ----
    // Gửi đúng 1 gói, chờ ACK (window = 1, không congestion control).
    bool sendPacket(const uint8_t* data, size_t len);

    // ---- Tuỳ chỉnh tham số truyền vào RDTWindowSender ----
    void setMSS(size_t mss)                  { mss_         = mss;         }
    void setTimeoutMs(int ms)                { timeoutMs_   = ms;          }
    void setMaxRetries(int r)                { maxRetries_  = r;           }
    void setInitialCwnd(double c)            { initCwnd_    = c;           }
    void setInitialSsthresh(double s)        { initSsthresh_= s;           }
    void setMaxWindowSegments(uint32_t w)    { maxWinSegs_  = w;           }


    // ---- Thống kê sau lần gửi (chỉ áp dụng cho sendFile/sendBuffer) ----
    double   getFinalCwnd()      const { return lastCwnd_;       }
    int      getTotalTimeouts()  const { return lastTimeouts_;   }
    uint32_t getTotalSegsSent()  const { return lastSegsSent_;   }

private:
    // Tạo RDTWindowSender theo tham số hiện tại rồi chạy
    bool runWindowSender(const uint8_t* data, size_t len);

    UDPSocket&   socket_;
    std::string  destIP_;
    uint16_t     destPort_;

    // Tham số forwarded đến RDTWindowSender
    size_t   mss_          = RDT_CHUNK_SIZE;
    int      timeoutMs_    = 500;
    int      maxRetries_   = 16;
    double   initCwnd_     = 1.0;
    double   initSsthresh_ = 8.0;
    uint32_t maxWinSegs_   = 32;


    // Stats
    double   lastCwnd_     = 1.0;
    int      lastTimeouts_ = 0;
    uint32_t lastSegsSent_ = 0;
};

// ============================================================
//  RDTReceiver
//  Bọc RDTWindowReceiver để cung cấp receiveFile(), receiveBuffer()
//  và receivePacket() backward-compat.
// ============================================================
class RDTReceiver
{
public:
    explicit RDTReceiver(UDPSocket& socket);

    // ---- API chính ----

    // Nhận toàn bộ dữ liệu vào RAM (block đến FIN).
    bool receiveBuffer(std::vector<uint8_t>& outBuffer);

    // Nhận toàn bộ dữ liệu, ghi ra file.
    bool receiveFile(const std::string& outputPath);

    // ---- Backward-compat: nhận đúng 1 gói Stop-and-Wait ----
    bool receivePacket(std::vector<uint8_t>& outData,
                       std::string& senderIP, uint16_t& senderPort);

    // ---- Tuỳ chỉnh ----
    void setTimeoutMs(int ms)          { timeoutMs_     = ms; }
    void setAdvertisedWindow(uint16_t w){ advWindow_    = w;  }

private:
    UDPSocket&  socket_;
    int         timeoutMs_  = 5000;
    uint16_t    advWindow_  = 8;

    // State cho receivePacket Stop-and-Wait backward-compat
    uint32_t    expectedSeq_  = 0;
    uint32_t    lastAckSent_  = UINT32_MAX;

    void sendRawAck(uint32_t ackNum, uint16_t windowSize,
                    const std::string& ip, uint16_t port);
};