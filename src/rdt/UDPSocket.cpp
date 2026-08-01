#include "UDPSocket.h"
#include <cstring>
#include <cstdio>

UDPSocket::UDPSocket() : sock_(INVALID_SOCKET_VAL)
{
#if defined(_WIN32)
    static bool wsaInitialized = false;
    if (!wsaInitialized)
    {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            throw std::runtime_error("WSAStartup thất bại");
        }
        wsaInitialized = true;
    }
#endif
}

UDPSocket::~UDPSocket()
{
    close();
}

void UDPSocket::open()
{
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET_VAL)
    {
        throw std::runtime_error("Khong the tao UDP socket");
    }
}

void UDPSocket::bind(uint16_t port)
{
    if (sock_ == INVALID_SOCKET_VAL)
    {
        throw std::runtime_error("Socket chua duoc open() truoc khi bind()");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        throw std::runtime_error("Bind that bai tren port " + std::to_string(port));
    }
}

int UDPSocket::sendTo(const uint8_t* data, size_t len,
                       const std::string& destIP, uint16_t destPort)
{
    if (sock_ == INVALID_SOCKET_VAL)
    {
        throw std::runtime_error("Socket chua duoc open()");
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(destPort);

    if (inet_pton(AF_INET, destIP.c_str(), &destAddr.sin_addr) <= 0)
    {
        throw std::runtime_error("Dia chi IP dich khong hop le: " + destIP);
    }

    int sentBytes = static_cast<int>(
        sendto(sock_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0,
               reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr))
    );

    return sentBytes;
}

int UDPSocket::recvFrom(uint8_t* buffer, size_t bufferSize,
                         std::string& outSenderIP, uint16_t& outSenderPort)
{
    if (sock_ == INVALID_SOCKET_VAL)
    {
        throw std::runtime_error("Socket chua duoc open()");
    }

    sockaddr_in senderAddr{};
    socklen_t senderAddrLen = sizeof(senderAddr);

    int recvBytes = static_cast<int>(
        recvfrom(sock_, reinterpret_cast<char*>(buffer), static_cast<int>(bufferSize), 0,
                 reinterpret_cast<sockaddr*>(&senderAddr), &senderAddrLen)
    );

    if (recvBytes >= 0)
    {
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &senderAddr.sin_addr, ipStr, sizeof(ipStr));
        outSenderIP = ipStr;
        outSenderPort = ntohs(senderAddr.sin_port);
    }

    return recvBytes;
}

void UDPSocket::setRecvTimeout(int milliseconds)
{
    if (sock_ == INVALID_SOCKET_VAL)
    {
        throw std::runtime_error("Socket chua duoc open()");
    }

#if defined(_WIN32)
    DWORD timeout = static_cast<DWORD>(milliseconds);
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval tv{};
    tv.tv_sec = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
#endif
}

uint16_t UDPSocket::getLocalPort() const
{
    if (sock_ == INVALID_SOCKET_VAL)
    {
        return 0;
    }

    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(sock_, reinterpret_cast<sockaddr*>(&addr), &len) < 0)
    {
        return 0;
    }
    return ntohs(addr.sin_port);
}

void UDPSocket::close()
{
    if (sock_ != INVALID_SOCKET_VAL)
    {
#if defined(_WIN32)
        closesocket(sock_);
#else
        ::close(sock_);
#endif
        sock_ = INVALID_SOCKET_VAL;
    }
}