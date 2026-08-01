#pragma once
#include <cstdint>
#include <string>
#include <stdexcept>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VAL (-1)
#endif

// ============================================================
//  UDPSocket — Wrapper đơn giản cho raw UDP socket.
//  Dùng chung cho cả UDP Client & UDP Server trong data channel.
//  Nhiệm vụ: mở/đóng socket, gửi/nhận gói tin thô (raw bytes).
//  (Chưa xử lý reliability - phần đó nằm ở ReliableTransfer.cpp)
// ============================================================
class UDPSocket
{
public:
    UDPSocket();
    ~UDPSocket();

    // Không cho copy (socket là tài nguyên hệ thống, không nên copy)
    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    // Mở socket UDP. Ném std::runtime_error nếu thất bại.
    void open();

    // Gắn socket vào 1 cổng cụ thể (dùng cho Server, hoặc Client muốn cổng cố định).
    // port = 0 => hệ điều hành tự chọn cổng trống (ephemeral port).
    void bind(uint16_t port);

    // Gửi 1 gói tin thô tới địa chỉ/cổng đích. Trả về số byte đã gửi.
    int sendTo(const uint8_t* data, size_t len,
               const std::string& destIP, uint16_t destPort);

    // Nhận 1 gói tin thô. Chặn (blocking) cho tới khi có dữ liệu tới.
    // outSenderIP / outSenderPort sẽ được set thành địa chỉ của nơi gửi.
    // Trả về số byte thực nhận được, hoặc -1 nếu lỗi.
    int recvFrom(uint8_t* buffer, size_t bufferSize,
                 std::string& outSenderIP, uint16_t& outSenderPort);

    // Đặt timeout cho recvFrom (mili-giây). 0 = chờ vô hạn (mặc định).
    void setRecvTimeout(int milliseconds);

    // Lấy cổng cục bộ thực tế đang dùng (hữu ích khi bind(0)).
    uint16_t getLocalPort() const;

    // Đóng socket.
    void close();

    bool isOpen() const { return sock_ != INVALID_SOCKET_VAL; }

private:
    socket_t sock_;
};