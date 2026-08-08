#pragma once
#include "UDPSocket.h"
#include "CustomUDPHeader.h"
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

// ============================================================
//  RDTWindowSender / RDTWindowReceiver   (Giai đoạn 3 - Dev2)
//  ------------------------------------------------------------
//  Nâng cấp từ Stop-and-Wait (ReliableTransfer.cpp, cửa sổ = 1)
//  lên Sliding Window, bổ sung:
//
//  1) KIỂM SOÁT LUỒNG (flow control):
//     Receiver quảng bá số "chỗ trống" còn nhận được qua trường
//     `windowSize` sẵn có trong CustomUDPHeader (giống rwnd của
//     TCP). Sender không bao giờ để số gói đang bay (in-flight)
//     vượt quá con số này.
//
//  2) KIỂM SOÁT TẮC NGHẼN (congestion control):
//     Sender tự quản lý "congestion window" (cwnd) theo mô hình
//     Slow Start + Congestion Avoidance (AIMD) kiểu TCP Reno rút gọn:
//       - Slow start   : mỗi ACK mới -> cwnd tăng theo cấp số nhân
//                         (cho tới khi chạm ssthresh).
//       - Cong. avoid. : sau ssthresh -> cwnd tăng tuyến tính
//                         (+1/cwnd mỗi ACK).
//       - Mất gói/timeout: ssthresh = cwnd/2, cwnd reset về 1
//                         (Multiplicative Decrease).
//
//  3) GIAO THỨC NỀN: Go-Back-N với ACK dồn (cumulative ACK) —
//     phù hợp với format header hiện tại (ackNum là 1 số nguyên
//     duy nhất, không phải bitmap SACK như Selective Repeat).
//     Khi timeout, sender gửi lại TOÀN BỘ các gói từ `base` trở đi.
// ============================================================

class RDTWindowSender
{
public:
    RDTWindowSender(UDPSocket &socket, const std::string &destIP, uint16_t destPort);

    // Gửi toàn bộ buffer `data` (độ dài `totalLen`) một cách tin cậy,
    // dùng sliding window + flow/congestion control.
    // Trả về true nếu toàn bộ dữ liệu đã được receiver ACK thành công.
    bool sendData(const uint8_t *data, size_t totalLen);

    // ---- Cấu hình (đều có giá trị mặc định hợp lý) ----
    void setMSS(size_t mss) { mss_ = mss; }
    void setInitialCwnd(double cwnd) { initialCwnd_ = cwnd; }
    void setInitialSsthresh(double s) { initialSsthresh_ = s; }
    void setMaxWindowSegments(uint32_t w) { maxWindowSegments_ = w; }
    void setTimeoutMs(int ms) { timeoutMs_ = ms; }
    void setMaxRetransmitRounds(int r) { maxRetransmitRounds_ = r; }
    void setCancellationCallback(std::function<bool()> callback) {
        shouldCancel_ = std::move(callback);
    }

    // ---- Thống kê sau khi sendData() chạy xong (phục vụ debug/demo) ----
    double getFinalCwnd() const { return cwnd_; }
    int getTotalTimeouts() const { return totalTimeouts_; }
    uint32_t getTotalPacketsSent() const { return totalPacketsSent_; }

private:
    uint32_t computeEffectiveWindow() const;

    UDPSocket &socket_;
    std::string destIP_;
    uint16_t destPort_;

    size_t mss_ = 1024; // Max Segment Size (byte payload / gói)
    double initialCwnd_ = 1.0;
    double initialSsthresh_ = 8.0;
    uint32_t maxWindowSegments_ = 32; // trần cửa sổ (tránh cwnd tăng vô hạn)
    int timeoutMs_ = 500;
    int maxRetransmitRounds_ = 16; // tổng số vòng retransmit tối đa trước khi bỏ cuộc

    // Trạng thái runtime (được reset mỗi lần gọi sendData)
    double cwnd_ = 1.0;
    double ssthresh_ = 8.0;
    uint32_t recvWindow_ = 1; // cửa sổ receiver quảng bá gần nhất (flow control)
    int totalTimeouts_ = 0;
    uint32_t totalPacketsSent_ = 0;
    std::function<bool()> shouldCancel_;
};

class RDTWindowReceiver
{
public:
    // advertisedWindow: số lượng segment tối đa receiver sẵn sàng nhận
    // cùng lúc — dùng cho flow control (gửi ngược lại cho sender qua
    // header.windowSize trong mỗi ACK).
    explicit RDTWindowReceiver(UDPSocket &socket, uint16_t advertisedWindow = 8);

    // Nhận dữ liệu cho tới khi gặp gói cuối (FLAG_FIN), ghép lại vào outData.
    // Hàm block cho tới khi nhận trọn vẹn 1 luồng dữ liệu kết thúc bằng FIN.
    // Trả về true nếu nhận thành công (luôn true khi thoát bình thường,
    // vì hàm chỉ return khi đã nhận đủ và đúng thứ tự tới gói FIN).
    bool receiveData(std::vector<uint8_t> &outData,
                     std::string &senderIP, uint16_t &senderPort);

    void setAdvertisedWindow(uint16_t w) { advertisedWindow_ = w; }
    void setCancellationCallback(std::function<bool()> callback) {
        shouldCancel_ = std::move(callback);
    }

private:
    void sendCumulativeAck(const std::string &ip, uint16_t port);

    UDPSocket &socket_;
    uint16_t advertisedWindow_;
    uint32_t expectedSeq_ = 0; // seq kế tiếp đang mong đợi (tăng dần theo từng segment, bắt đầu từ 0)
    std::function<bool()> shouldCancel_;
};
