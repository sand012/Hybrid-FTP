// ============================================================
//  ReliableTransfer.cpp
//  Lớp bọc mỏng: RDTSender / RDTReceiver delegate hoàn toàn sang
//  RDTWindowSender / RDTWindowReceiver (SlidingWindow.h/cpp).
//
//  sendPacket() / receivePacket() giữ lại Stop-and-Wait để tương
//  thích ngược với RdtClientTest / RdtSeverTest (demo giai đoạn 2).
// ============================================================
#include "ReliableTransfer.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

// ============================================================
//  RDTSender
// ============================================================

RDTSender::RDTSender(UDPSocket& socket,
                     const std::string& destIP,
                     uint16_t destPort)
    : socket_(socket), destIP_(destIP), destPort_(destPort)
{
}

bool RDTSender::runWindowSender(const uint8_t* data, size_t len)
{
    RDTWindowSender ws(socket_, destIP_, destPort_);
    ws.setMSS(mss_);
    ws.setTimeoutMs(timeoutMs_);
    ws.setMaxRetransmitRounds(maxRetries_);
    ws.setInitialCwnd(initCwnd_);
    ws.setInitialSsthresh(initSsthresh_);
    ws.setMaxWindowSegments(maxWinSegs_);
    ws.setCancellationCallback(shouldCancel_);

    bool ok = ws.sendData(data, len);

    // Lưu thống kê
    lastCwnd_     = ws.getFinalCwnd();
    lastTimeouts_ = ws.getTotalTimeouts();
    lastSegsSent_ = ws.getTotalPacketsSent();

    return ok;
}

bool RDTSender::sendBuffer(const uint8_t* data, size_t totalLen)
{
    printf("[RDT-SENDER] sendBuffer: %zu bytes\n", totalLen);
    return runWindowSender(data, totalLen);
}

bool RDTSender::sendFile(const std::string& filePath)
{
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
    {
        fprintf(stderr, "[RDT-SENDER] Khong the mo file: %s\n", filePath.c_str());
        return false;
    }

    std::streamsize fileSize = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(fileSize));
    if (fileSize > 0 && !ifs.read(reinterpret_cast<char*>(buf.data()), fileSize))
    {
        fprintf(stderr, "[RDT-SENDER] Loi doc file: %s\n", filePath.c_str());
        return false;
    }
    ifs.close();

    printf("[RDT-SENDER] sendFile: '%s' (%lld bytes)\n",
           filePath.c_str(), static_cast<long long>(fileSize));
    return runWindowSender(buf.data(), buf.size());
}

// ---- Stop-and-Wait backward-compat ----
// Gửi đúng 1 gói, chờ ACK với vòng lặp timeout + retransmit.
// Thử tối đa (1 + maxRetries_) lần trước khi báo lỗi hẳn.
bool RDTSender::sendPacket(const uint8_t* data, size_t len)
{
    // Reset thống kê của lần gọi này
    retryCount_ = 0;

    // ---- Xây packet (header + payload) ----
    CustomUDPHeader hdr{};
    hdr.seqNum     = 0;          // Stop-and-Wait dùng seq cố định = 0
    hdr.ackNum     = 0;
    hdr.payloadLen = static_cast<uint16_t>(len);
    hdr.windowSize = 1;
    setFlag(hdr, FLAG_DATA);
    hdr.checksum   = calculateChecksum(hdr, data, len);

    // Buffer = header + payload
    std::vector<uint8_t> pkt(CUSTOM_UDP_HEADER_SIZE + len);
    serializeHeader(hdr, pkt.data());
    if (len > 0)
        std::memcpy(pkt.data() + CUSTOM_UDP_HEADER_SIZE, data, len);

    constexpr size_t ACK_BUF_SZ = CUSTOM_UDP_HEADER_SIZE + 64;
    uint8_t ackBuf[ACK_BUF_SZ];

    // ---- Vòng lặp gửi + chờ ACK (tối đa 1 + maxRetries_ lần) ----
    for (int attempt = 0; attempt <= maxRetries_; ++attempt)
    {
        if (attempt == 0)
            printf("[RDT-SENDER] sendPacket: gui goi seq=0, len=%zu\n", len);
        else
            printf("[RDT-SENDER] sendPacket: retransmit lan %d/%d (seq=0)\n",
                   attempt, maxRetries_);

        // Gửi packet
        int sent = socket_.sendTo(pkt.data(), pkt.size(), destIP_, destPort_);
        if (sent < 0)
        {
            fprintf(stderr, "[RDT-SENDER] sendPacket: sendTo that bai (attempt=%d)\n",
                    attempt);
            // Không break ngay, thử lại lần sau
            retryCount_ = attempt;
            continue;
        }

        // Đặt timeout và chờ ACK
        socket_.setRecvTimeout(timeoutMs_);

        std::string fromIP;
        uint16_t    fromPort = 0;
        int received = socket_.recvFrom(ackBuf, ACK_BUF_SZ, fromIP, fromPort);

        if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
        {
            // Timeout hoặc gói quá nhỏ → retry
            printf("[RDT-SENDER] sendPacket: timeout hoac goi ACK qua nho"
                   " (attempt=%d)\n", attempt);
            retryCount_ = attempt + 1;   // đã cần ít nhất (attempt+1) lần retry
            continue;
        }

        CustomUDPHeader ackHdr = deserializeHeader(ackBuf);

        // Kiểm tra ACK hợp lệ: flag ACK, ackNum đúng, checksum đúng
        if (!hasFlag(ackHdr, FLAG_ACK))
        {
            printf("[RDT-SENDER] sendPacket: nhan duoc goi khong phai ACK"
                   " (attempt=%d)\n", attempt);
            retryCount_ = attempt + 1;
            continue;
        }
        if (ackHdr.ackNum != hdr.seqNum)
        {
            printf("[RDT-SENDER] sendPacket: ACK sai seq (nhan=%u, mong=%u)"
                   " (attempt=%d)\n", ackHdr.ackNum, hdr.seqNum, attempt);
            retryCount_ = attempt + 1;
            continue;
        }
        if (!verifyChecksum(ackHdr, ackBuf + CUSTOM_UDP_HEADER_SIZE,
                            static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE))
        {
            printf("[RDT-SENDER] sendPacket: ACK checksum SAI (attempt=%d)\n",
                   attempt);
            retryCount_ = attempt + 1;
            continue;
        }

        // ---- ACK hợp lệ ----
        printf("[RDT-SENDER] sendPacket: nhan ACK hop le (attempt=%d,"
               " retries=%d)\n", attempt, retryCount_);
        lastSegsSent_ = 1;
        return true;
    }

    // Hết số lần retry
    fprintf(stderr,
            "[RDT-SENDER] sendPacket: THAT BAI sau %d lan thu (maxRetries=%d)\n",
            retryCount_, maxRetries_);
    lastSegsSent_ = 1;
    return false;
}

// ============================================================
//  RDTReceiver
// ============================================================

RDTReceiver::RDTReceiver(UDPSocket& socket)
    : socket_(socket), expectedSeq_(0), lastAckSent_(UINT32_MAX)
{
}

void RDTReceiver::sendRawAck(uint32_t ackNum, uint16_t windowSize,
                              const std::string& ip, uint16_t port)
{
    CustomUDPHeader ack{};
    ack.seqNum     = 0;
    ack.ackNum     = ackNum;
    ack.payloadLen = 0;
    ack.windowSize = windowSize;
    setFlag(ack, FLAG_ACK);
    ack.checksum   = calculateChecksum(ack, nullptr, 0);

    uint8_t buf[CUSTOM_UDP_HEADER_SIZE];
    serializeHeader(ack, buf);
    socket_.sendTo(buf, sizeof(buf), ip, port);
}

bool RDTReceiver::receiveBuffer(std::vector<uint8_t>& outBuffer)
{
    RDTWindowReceiver wr(socket_, advWindow_);
    wr.setCancellationCallback(shouldCancel_);
    socket_.setRecvTimeout(timeoutMs_);

    std::string senderIP;
    uint16_t senderPort = 0;
    return wr.receiveData(outBuffer, senderIP, senderPort);
}

bool RDTReceiver::receiveFile(const std::string& outputPath)
{
    std::vector<uint8_t> buffer;
    if (!receiveBuffer(buffer))
        return false;

    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open())
    {
        fprintf(stderr, "[RDT-RECEIVER] Khong the ghi file: %s\n", outputPath.c_str());
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    ofs.close();

    printf("[RDT-RECEIVER] Da ghi %zu bytes ra file '%s'\n",
           buffer.size(), outputPath.c_str());
    return true;
}

// ---- Stop-and-Wait backward-compat ----
bool RDTReceiver::receivePacket(std::vector<uint8_t>& outData,
                                std::string& senderIP, uint16_t& senderPort)
{
    constexpr size_t BUF_SZ = CUSTOM_UDP_HEADER_SIZE + 4096;
    uint8_t recvBuf[BUF_SZ];

    int received = socket_.recvFrom(recvBuf, BUF_SZ, senderIP, senderPort);
    if (received < static_cast<int>(CUSTOM_UDP_HEADER_SIZE))
    {
        printf("[RDT-RECEIVER] Goi qua nho hoac loi nhan.\n");
        return false;
    }

    CustomUDPHeader hdr    = deserializeHeader(recvBuf);
    const uint8_t*  payload    = recvBuf + CUSTOM_UDP_HEADER_SIZE;
    size_t          payloadLen = static_cast<size_t>(received) - CUSTOM_UDP_HEADER_SIZE;

    if (!verifyChecksum(hdr, payload, payloadLen))
    {
        printf("[RDT-RECEIVER] Checksum SAI (seq=%u)\n", hdr.seqNum);
        return false;
    }

    if (hdr.seqNum == expectedSeq_)
    {
        outData.assign(payload, payload + payloadLen);
        sendRawAck(hdr.seqNum, advWindow_, senderIP, senderPort);
        printf("[RDT-RECEIVER] Nhan goi MOI seq=%u (%zu B) -> ACK(%u)\n",
               hdr.seqNum, payloadLen, hdr.seqNum);
        lastAckSent_ = hdr.seqNum;
        ++expectedSeq_;
        return true;
    }

    printf("[RDT-RECEIVER] Goi TRUNG seq=%u (mong doi=%u) -> gui lai ACK(%u)\n",
           hdr.seqNum, expectedSeq_,
           (lastAckSent_ == UINT32_MAX) ? 0u : lastAckSent_);
    if (lastAckSent_ != UINT32_MAX)
        sendRawAck(lastAckSent_, advWindow_, senderIP, senderPort);
    return false;
}
