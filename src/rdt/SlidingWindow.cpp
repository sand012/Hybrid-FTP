// ============================================================
//  SlidingWindow.cpp — Triển khai RDTWindowSender & RDTWindowReceiver
//
//  Thuật toán:
//    Sender : Go-Back-N + Slow Start / AIMD (TCP Reno rút gọn)
//    Receiver: GBN cumulative ACK, quảng bá advertisedWindow (flow control)
// ============================================================
#include "SlidingWindow.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <fstream>
#include <algorithm>

// ============================================================
//  Helpers nội bộ
// ============================================================

// Đóng gói 1 segment DATA và gửi đi
static bool sendSegment(UDPSocket& sock,
                        const std::string& ip, uint16_t port,
                        uint32_t seqNum,
                        const uint8_t* payload, size_t payloadLen,
                        uint16_t senderWindow)
{
    CustomUDPHeader hdr{};
    hdr.seqNum     = seqNum;
    hdr.ackNum     = 0;
    hdr.payloadLen = static_cast<uint16_t>(payloadLen);
    hdr.windowSize = senderWindow;
    setFlag(hdr, FLAG_DATA);
    hdr.checksum   = calculateChecksum(hdr, payload, payloadLen);

    std::vector<uint8_t> buf(CUSTOM_UDP_HEADER_SIZE + payloadLen);
    serializeHeader(hdr, buf.data());
    if (payloadLen > 0)
        std::memcpy(buf.data() + CUSTOM_UDP_HEADER_SIZE, payload, payloadLen);

    return sock.sendTo(buf.data(), buf.size(), ip, port) >= 0;
}

// Đóng gói FIN và gửi đi
static bool sendFIN(UDPSocket& sock,
                    const std::string& ip, uint16_t port,
                    uint32_t seqNum, uint16_t senderWindow)
{
    CustomUDPHeader fin{};
    fin.seqNum     = seqNum;
    fin.ackNum     = 0;
    fin.payloadLen = 0;
    fin.windowSize = senderWindow;
    setFlag(fin, FLAG_FIN);
    fin.checksum   = calculateChecksum(fin, nullptr, 0);

    uint8_t buf[CUSTOM_UDP_HEADER_SIZE];
    serializeHeader(fin, buf);
    return sock.sendTo(buf, sizeof(buf), ip, port) >= 0;
}

// ============================================================
//  RDTWindowSender — Constructor
// ============================================================
RDTWindowSender::RDTWindowSender(UDPSocket& socket,
                                 const std::string& destIP,
                                 uint16_t destPort)
    : socket_(socket), destIP_(destIP), destPort_(destPort)
{
}

// ============================================================
//  RDTWindowSender::computeEffectiveWindow
//  Cửa sổ hiệu dụng = min(cwnd, recvWindow, maxWindowSegments)
// ============================================================
uint32_t RDTWindowSender::computeEffectiveWindow() const
{
    uint32_t cw  = static_cast<uint32_t>(std::max(1.0, std::floor(cwnd_)));
    uint32_t rw  = (recvWindow_ == 0) ? 1u : recvWindow_; // tránh deadlock
    uint32_t eff = std::min({cw, rw, maxWindowSegments_});
    return (eff == 0) ? 1u : eff;
}

// ============================================================
//  RDTWindowSender::sendData
//  Pipeline chính: phân đoạn → GBN + Slow Start / AIMD
// ============================================================
bool RDTWindowSender::sendData(const uint8_t* data, size_t totalLen)
{
    // ---- Chia dữ liệu thành segments ----
    std::vector<std::vector<uint8_t>> segs;
    {
        size_t offset = 0;
        while (offset < totalLen)
        {
            size_t sz = std::min(mss_, totalLen - offset);
            segs.emplace_back(data + offset, data + offset + sz);
            offset += sz;
        }
    }
    if (segs.empty())
        segs.push_back({}); // ít nhất 1 gói rỗng

    const uint32_t N = static_cast<uint32_t>(segs.size());

    // ---- Reset trạng thái congestion control ----
    cwnd_             = initialCwnd_;
    ssthresh_         = initialSsthresh_;
    recvWindow_       = static_cast<uint32_t>(initialSsthresh_); // giả định ban đầu
    totalTimeouts_    = 0;
    totalPacketsSent_ = 0;

    printf("[SW-SENDER] Bat dau gui %u segment (MSS=%zu, cwnd=%.1f, ssthresh=%.1f)\n",
           N, mss_, cwnd_, ssthresh_);

    socket_.setRecvTimeout(timeoutMs_);

    uint32_t base     = 0;  // seq nhỏ nhất chưa ACK
    uint32_t nextSeq  = 0;  // seq kế tiếp sẽ gửi
    int      retryRnd = 0;  // số vòng retransmit liên tiếp

    while (base < N)
    {
        if (shouldCancel_ && shouldCancel_()) {
            printf("[SW-SENDER] Transfer bi huy bo boi ABOR.\n");
            return false;
        }
        uint32_t wnd = computeEffectiveWindow();

        // ---- Gửi các segment trong cửa sổ ----
        while (nextSeq < N && nextSeq < base + wnd)
        {
            const auto& seg = segs[nextSeq];
            bool ok = sendSegment(socket_, destIP_, destPort_,
                                  nextSeq,
                                  seg.data(), seg.size(),
                                  static_cast<uint16_t>(wnd));
            if (!ok)
            {
                fprintf(stderr, "[SW-SENDER] Loi gui segment seq=%u\n", nextSeq);
                return false;
            }
            printf("[SW-SENDER] Gui seq=%u (%zu B) | cwnd=%.2f ssthresh=%.2f wnd=%u\n",
                   nextSeq, seg.size(), cwnd_, ssthresh_, wnd);
            ++totalPacketsSent_;
            ++nextSeq;
        }

        // ---- Chờ ACK ----
        uint8_t recvBuf[CUSTOM_UDP_HEADER_SIZE + 64];
        std::string fromIP;
        uint16_t fromPort;
        int received = socket_.recvFrom(recvBuf, sizeof(recvBuf), fromIP, fromPort);

        if (shouldCancel_ && shouldCancel_()) {
            printf("[SW-SENDER] Transfer bi huy boi ABOR khi cho ACK.\n");
            return false;
        }

        if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
        {
            // -------- TIMEOUT → Multiplicative Decrease --------
            ++totalTimeouts_;
            ++retryRnd;
            printf("[SW-SENDER] TIMEOUT (vong %d/%d) | cwnd=%.2f -> ssthresh=%.2f, cwnd=1\n",
                   retryRnd, maxRetransmitRounds_, cwnd_, cwnd_ / 2.0);

            if (retryRnd > maxRetransmitRounds_)
            {
                fprintf(stderr, "[SW-SENDER] Qua so vong retransmit. Huy bo.\n");
                return false;
            }

            ssthresh_ = std::max(cwnd_ / 2.0, 1.0);
            cwnd_     = 1.0;   // Slow Start lại từ đầu
            nextSeq   = base;  // Go-Back-N
            continue;
        }

        CustomUDPHeader ackHdr = deserializeHeader(recvBuf);
        const uint8_t*  ap     = recvBuf + CUSTOM_UDP_HEADER_SIZE;
        size_t          apLen  = static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE;

        if (!verifyChecksum(ackHdr, ap, apLen))
        {
            printf("[SW-SENDER] ACK checksum sai, bo qua.\n");
            continue;
        }

        if (!hasFlag(ackHdr, FLAG_ACK))
        {
            printf("[SW-SENDER] Khong phai ACK, bo qua.\n");
            continue;
        }

        uint32_t ackNum    = ackHdr.ackNum;
        uint16_t rxWindow  = ackHdr.windowSize; // flow control từ receiver

        printf("[SW-SENDER] Nhan ACK(%u) | recvWin=%u | cwnd=%.2f ssthresh=%.2f\n",
               ackNum, rxWindow, cwnd_, ssthresh_);

        // Cập nhật receive window (flow control)
        if (rxWindow > 0)
            recvWindow_ = rxWindow;

        // ---- Cumulative ACK hợp lệ → slide cửa sổ ----
        if (ackNum >= base && ackNum < N)
        {
            uint32_t newAcked = ackNum + 1 - base; // số segment mới được ACK

            // -------- Congestion Control --------
            for (uint32_t i = 0; i < newAcked; ++i)
            {
                if (cwnd_ < ssthresh_)
                {
                    // Slow Start: tăng theo cấp số nhân
                    cwnd_ += 1.0;
                }
                else
                {
                    // Congestion Avoidance: tăng tuyến tính (AIMD)
                    cwnd_ += 1.0 / cwnd_;
                }
                cwnd_ = std::min(cwnd_, static_cast<double>(maxWindowSegments_));
            }

            base     = ackNum + 1;
            retryRnd = 0; // ACK thành công → reset retry
        }
        else if (ackNum == N - 1 && base <= ackNum)
        {
            // Trường hợp cuối
            uint32_t newAcked = ackNum + 1 - base;
            for (uint32_t i = 0; i < newAcked; ++i)
            {
                if (cwnd_ < ssthresh_) cwnd_ += 1.0;
                else                  cwnd_ += 1.0 / cwnd_;
                cwnd_ = std::min(cwnd_, static_cast<double>(maxWindowSegments_));
            }
            base = N;
            retryRnd = 0;
        }
    }

    printf("[SW-SENDER] Tat ca %u segment da ACK. cwnd_final=%.2f, timeouts=%d\n",
           N, cwnd_, totalTimeouts_);

    // ---- Gửi FIN và chờ FINACK ----
    socket_.setRecvTimeout(timeoutMs_);
    for (int attempt = 0; attempt < maxRetransmitRounds_; ++attempt)
    {
        if (shouldCancel_ && shouldCancel_())
            return false;
        sendFIN(socket_, destIP_, destPort_,
                N, static_cast<uint16_t>(computeEffectiveWindow()));
        printf("[SW-SENDER] Gui FIN (lan %d)\n", attempt + 1);

        uint8_t ackBuf[CUSTOM_UDP_HEADER_SIZE + 64];
        std::string fromIP; uint16_t fromPort;
        int r = socket_.recvFrom(ackBuf, sizeof(ackBuf), fromIP, fromPort);
        if (r >= static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
        {
            CustomUDPHeader h = deserializeHeader(ackBuf);
            if (hasFlag(h, FLAG_FIN) && hasFlag(h, FLAG_ACK))
            {
                printf("[SW-SENDER] Nhan FINACK -> Truyen hoan tat.\n");
                return true;
            }
        }
        printf("[SW-SENDER] Chua FINACK, thu lai...\n");
    }

    fprintf(stderr, "[SW-SENDER] Khong nhan duoc FINACK.\n");
    return false;
}

// ============================================================
//  RDTWindowReceiver — Constructor
// ============================================================
RDTWindowReceiver::RDTWindowReceiver(UDPSocket& socket, uint16_t advertisedWindow)
    : socket_(socket), advertisedWindow_(advertisedWindow), expectedSeq_(0)
{
}

// ============================================================
//  RDTWindowReceiver::sendCumulativeAck
//  Gửi ACK(expectedSeq_ - 1) kèm advertisedWindow vào header.windowSize
// ============================================================
void RDTWindowReceiver::sendCumulativeAck(const std::string& ip, uint16_t port)
{
    uint32_t ackNum = (expectedSeq_ == 0) ? 0 : (expectedSeq_ - 1);

    CustomUDPHeader ack{};
    ack.seqNum     = 0;
    ack.ackNum     = ackNum;
    ack.payloadLen = 0;
    ack.windowSize = advertisedWindow_; // quảng bá flow-control window
    setFlag(ack, FLAG_ACK);
    ack.checksum   = calculateChecksum(ack, nullptr, 0);

    uint8_t buf[CUSTOM_UDP_HEADER_SIZE];
    serializeHeader(ack, buf);
    socket_.sendTo(buf, sizeof(buf), ip, port);

    printf("[SW-RECEIVER] Gui ACK(%u) | advertisedWin=%u\n", ackNum, advertisedWindow_);
}

// ============================================================
//  RDTWindowReceiver::receiveData
//  Nhận stream cho đến FIN, ghép vào outData.
// ============================================================
bool RDTWindowReceiver::receiveData(std::vector<uint8_t>& outData,
                                    std::string& senderIP, uint16_t& senderPort)
{
    outData.clear();
    expectedSeq_ = 0;

    // Buffer đủ cho 1 segment (MSS tối đa 1500 - header IP/UDP ~ 1472)
    constexpr size_t MAX_SEG = 1500;
    uint8_t recvBuf[CUSTOM_UDP_HEADER_SIZE + MAX_SEG + 64];

    while (true)
    {
        if (shouldCancel_ && shouldCancel_()) {
            printf("[SW-RECEIVER] Transfer bi huy boi ABOR.\n");
            return false;
        }
        int received = socket_.recvFrom(recvBuf, sizeof(recvBuf), senderIP, senderPort);

        if (shouldCancel_ && shouldCancel_()) {
            printf("[SW-RECEIVER] Transfer bi huy boi ABOR khi cho data.\n");
            return false;
        }

        if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
        {
            // Timeout — gửi lại ACK để kích thích sender gửi tiếp
            printf("[SW-RECEIVER] Timeout hoac goi qua nho. Gui lai ACK.\n");
            if (expectedSeq_ > 0)
                sendCumulativeAck(senderIP, senderPort);
            continue;
        }

        CustomUDPHeader hdr    = deserializeHeader(recvBuf);
        const uint8_t*  payload    = recvBuf + CUSTOM_UDP_HEADER_SIZE;
        size_t          payloadLen = static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE;

        // ---- Checksum ----
        if (!verifyChecksum(hdr, payload, payloadLen))
        {
            printf("[SW-RECEIVER] Checksum sai (seq=%u) -> gui lai ACK cu.\n", hdr.seqNum);
            if (expectedSeq_ > 0)
                sendCumulativeAck(senderIP, senderPort);
            continue;
        }

        // ---- FIN: kết thúc ----
        if (hasFlag(hdr, FLAG_FIN))
        {
            printf("[SW-RECEIVER] Nhan FIN -> Truyen hoan tat, %zu bytes.\n", outData.size());

            // Gửi FINACK
            CustomUDPHeader finAck{};
            finAck.seqNum     = 0;
            finAck.ackNum     = hdr.seqNum;
            finAck.payloadLen = 0;
            finAck.windowSize = advertisedWindow_;
            setFlag(finAck, FLAG_FIN);
            setFlag(finAck, FLAG_ACK);
            finAck.checksum   = calculateChecksum(finAck, nullptr, 0);
            uint8_t finBuf[CUSTOM_UDP_HEADER_SIZE];
            serializeHeader(finAck, finBuf);
            socket_.sendTo(finBuf, sizeof(finBuf), senderIP, senderPort);
            return true;
        }

        // ---- DATA ----
        if (!hasFlag(hdr, FLAG_DATA))
        {
            printf("[SW-RECEIVER] Goi khong phai DATA/FIN, bo qua (flags=0x%02x).\n",
                   hdr.flags);
            continue;
        }

        if (hdr.seqNum == expectedSeq_)
        {
            // Gói đúng thứ tự → nhận vào buffer
            outData.insert(outData.end(), payload, payload + payloadLen);
            printf("[SW-RECEIVER] Nhan seq=%u (%zu B) | tong=%zu B\n",
                   hdr.seqNum, payloadLen, outData.size());
            ++expectedSeq_;
            sendCumulativeAck(senderIP, senderPort);
        }
        else
        {
            // Gói lệch thứ tự → GBN: bỏ qua, gửi lại ACK của gói cuối đã nhận
            printf("[SW-RECEIVER] Nhan seq=%u (mong doi=%u) -> Go-Back-N: gui lai ACK(%u)\n",
                   hdr.seqNum, expectedSeq_,
                   (expectedSeq_ > 0) ? (expectedSeq_ - 1) : 0u);
            if (expectedSeq_ > 0)
                sendCumulativeAck(senderIP, senderPort);
        }
    }
}
