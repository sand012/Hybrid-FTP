#include "ClientCLI.h"

#include "../common/CryptoHash.h"
#include "../rdt/ReliableTransfer.h"
#include "../rdt/UDPSocket.h"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

// ============================================================
// Đọc một reply từ TCP control channel.
//
// Dùng sau khi data transfer hoàn thành để đọc reply cuối
// như 226 Transfer complete.
// ============================================================
static std::string recvControlReply(int fd)
{
    std::string reply;
    char ch;

    while (true)
    {
        ssize_t n = recv(fd, &ch, 1, 0);

        if (n == 0)
        {
            break;
        }

        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            break;
        }

        reply.push_back(ch);

        if (ch == '\n')
        {
            break;
        }
    }

    return reply;
}

bool ClientCLI::sendAbortWithoutWaiting()
{
    if (m_abortCommandSent.exchange(true))
        return true;

    static constexpr char command[] = "ABOR\r\n";
    std::size_t sent = 0;
    while (sent < sizeof(command) - 1)
    {
        const ssize_t n = send(m_controlSock, command + sent,
                               sizeof(command) - 1 - sent, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

// ============================================================
// Constructor
// ============================================================
ClientCLI::ClientCLI(CommandSender sender, int controlSock)
    : m_sendCommand(std::move(sender)),
      m_controlSock(controlSock)
{
    // Lấy IP server từ TCP socket
    sockaddr_in peer{};
    socklen_t len = sizeof(peer);

    if (getpeername(
            controlSock,
            reinterpret_cast<sockaddr *>(&peer),
            &len) == 0)
    {
        char buf[INET_ADDRSTRLEN];

        inet_ntop(
            AF_INET,
            &peer.sin_addr,
            buf,
            sizeof(buf));

        m_serverHost = buf;
    }
    else
    {
        m_serverHost = "127.0.0.1";
    }
}

// ============================================================
// Parse:
// 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2).
// ============================================================
uint16_t ClientCLI::parsePasvReply(
    const std::string &reply,
    std::string &outIP) const
{
    std::regex re(
        R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))");

    std::smatch m;

    if (!std::regex_search(reply, m, re))
    {
        return 0;
    }

    outIP =
        m[1].str() + "." +
        m[2].str() + "." +
        m[3].str() + "." +
        m[4].str();

    uint16_t p1 =
        static_cast<uint16_t>(std::stoul(m[5].str()));

    uint16_t p2 =
        static_cast<uint16_t>(std::stoul(m[6].str()));

    return static_cast<uint16_t>((p1 << 8) | p2);
}

// ============================================================
// RETR
//
// Tải file từ server về client.
//
// Flow:
// PASV
//   ↓
// 227
//   ↓
// mở UDP socket
//   ↓
// knock passive port
//   ↓
// RETR
//   ↓
// 150
//   ↓
// RDT receive
//   ↓
// 226
// ============================================================
std::string ClientCLI::handleRetr(
    const std::string &remoteFile,
    const std::string &localFile)
{
    // ========================================================
    // 1. PASV
    // ========================================================
    std::string pasvReply =
        m_sendCommand("PASV");

    if (pasvReply.empty() ||
        pasvReply.rfind("227", 0) != 0)
    {
        return "Error: PASV failed: " + pasvReply;
    }

    std::string serverIP;

    uint16_t pasvPort =
        parsePasvReply(pasvReply, serverIP);

    if (pasvPort == 0)
    {
        return "Error: Cannot parse PASV reply: " +
               pasvReply;
    }

    // ========================================================
    // 2. Mở UDP socket phía client
    // ========================================================
    UDPSocket udp;

    try
    {
        udp.open();
        udp.bind(0);
        udp.setRecvTimeout(15000);
    }
    catch (const std::exception &e)
    {
        return std::string(
                   "Error: Cannot open UDP socket: ") +
               e.what();
    }

    // ========================================================
    // 3. Knock passive UDP socket
    //
    // Server đang kiểm tra packet phải >=
    // CUSTOM_UDP_HEADER_SIZE.
    // ========================================================
    uint8_t knock[CUSTOM_UDP_HEADER_SIZE]{};

    udp.sendTo(
        knock,
        sizeof(knock),
        serverIP,
        pasvPort);

    // ========================================================
    // 4. Gửi RETR
    // ========================================================
    std::string retrReply =
        m_sendCommand(
            "RETR " + remoteFile);

    if (retrReply.empty())
    {
        return "Error: No reply to RETR";
    }

    // Server phải trả 150 trước khi transfer
    if (retrReply.rfind("150", 0) != 0)
    {
        // Có thể là 550, 425,...
        return retrReply;
    }

    std::printf(
        "[CLIENT] Nhan file '%s' <- '%s' tu %s:%u\n",
        localFile.c_str(),
        remoteFile.c_str(),
        serverIP.c_str(),
        pasvPort);

    // ========================================================
    // 5. Nhận dữ liệu bằng RDT
    // ========================================================
    RDTReceiver receiver(udp);
    receiver.setTimeoutMs(250);
    receiver.setCancellationCallback([this]() {
        if (!m_aborted.load())
            return false;
        sendAbortWithoutWaiting();
        return true;
    });

    std::vector<uint8_t> buffer;

    bool ok =
        receiver.receiveBuffer(buffer);

    if (!ok)
    {
        if (m_abortCommandSent.load())
        {
            const std::string aborted = recvControlReply(m_controlSock);
            const std::string completed = recvControlReply(m_controlSock);
            return retrReply + "\n" + aborted + "\n" + completed;
        }
        return retrReply +
               "\nError: RDT receive failed";
    }

    std::vector<uint8_t> decoded;
    std::string codecError;
    if (!TransferModeCodec::decode(buffer, m_transferMode, m_transferType,
                                   decoded, &codecError))
    {
        return retrReply + "\nError: MODE decode failed: " + codecError;
    }
    buffer = std::move(decoded);

    // ========================================================
    // 6. Ghi file local
    // ========================================================
    std::ofstream ofs(
        localFile,
        std::ios::binary);

    if (!ofs.is_open())
    {
        return "Error: Cannot create local file: " +
               localFile;
    }

    if (!buffer.empty())
    {
        ofs.write(
            reinterpret_cast<const char *>(
                buffer.data()),
            static_cast<std::streamsize>(
                buffer.size()));
    }

    ofs.close();

    // ========================================================
    // 7. Hash file nhận được
    // ========================================================
    std::vector<char> fileData(
        buffer.begin(),
        buffer.end());

    std::string hash =
        CryptoHash::computeSHA256(fileData);

    std::printf(
        "[CLIENT] SHA-256 sau RETR: %s (%zu bytes)\n",
        hash.c_str(),
        buffer.size());

    // ========================================================
    // 8. Đọc reply cuối THẬT từ server
    //
    // Không được tự hard-code 226.
    // ========================================================
    std::string finalReply =
        recvControlReply(m_controlSock);

    if (finalReply.empty())
    {
        return retrReply +
               "\nError: Missing final server reply.";
    }

    return retrReply +
           "\n" +
           finalReply;
}

// ============================================================
// STOR / STOU / APPE
//
// Upload file từ client lên server.
// ============================================================
std::string ClientCLI::handleStor(
    const std::string &localFile,
    const std::string &remoteFile,
    bool unique,
    bool append)
{
    // ========================================================
    // 1. Đọc file local
    // ========================================================
    std::ifstream ifs(
        localFile,
        std::ios::binary | std::ios::ate);

    if (!ifs.is_open())
    {
        return "Error: Local file not found: " +
               localFile;
    }

    std::streamsize fileSize =
        ifs.tellg();

    ifs.seekg(
        0,
        std::ios::beg);

    std::vector<uint8_t> fileBuffer(
        static_cast<size_t>(fileSize));

    if (fileSize > 0 &&
        !ifs.read(
            reinterpret_cast<char *>(
                fileBuffer.data()),
            fileSize))
    {
        return "Error: Cannot read local file: " +
               localFile;
    }

    ifs.close();

    // ========================================================
    // Hash trước transfer
    // ========================================================
    std::vector<char> charBuffer(
        fileBuffer.begin(),
        fileBuffer.end());

    std::string hashBefore =
        CryptoHash::computeSHA256(charBuffer);

    std::printf(
        "[CLIENT] SHA-256 truoc STOR: %s (%lld bytes)\n",
        hashBefore.c_str(),
        static_cast<long long>(fileSize));

    // ========================================================
    // 2. PASV
    // ========================================================
    std::string pasvReply =
        m_sendCommand("PASV");

    if (pasvReply.empty() ||
        pasvReply.rfind("227", 0) != 0)
    {
        return "Error: PASV failed: " +
               pasvReply;
    }

    std::string serverIP;

    uint16_t pasvPort =
        parsePasvReply(
            pasvReply,
            serverIP);

    if (pasvPort == 0)
    {
        return "Error: Cannot parse PASV reply: " +
               pasvReply;
    }

    // ========================================================
    // 3. Xây dựng command
    // ========================================================
    std::string command;

    if (unique)
    {
        command =
            remoteFile.empty()
                ? "STOU"
                : "STOU " + remoteFile;
    }
    else if (append)
    {
        command =
            "APPE " + remoteFile;
    }
    else
    {
        command =
            "STOR " + remoteFile;
    }

    // ========================================================
    // 4. Gửi command
    // ========================================================
    std::string openingReply =
        m_sendCommand(command);

    if (openingReply.empty())
    {
        return "Error: No reply to " +
               command;
    }

    if (openingReply.rfind("150", 0) != 0 &&
        openingReply.rfind("125", 0) != 0)
    {
        return openingReply;
    }

    std::printf(
        "[CLIENT] Gui file '%s' -> '%s' den %s:%u\n",
        localFile.c_str(),
        remoteFile.c_str(),
        serverIP.c_str(),
        pasvPort);

    // ========================================================
    // 5. Mở UDP socket
    // ========================================================
    UDPSocket udp;

    try
    {
        udp.open();
        udp.bind(0);
        udp.setRecvTimeout(15000);
    }
    catch (const std::exception &e)
    {
        return std::string(
                   "Error: Cannot open UDP socket: ") +
               e.what();
    }

    // ========================================================
    // 6. Gửi dữ liệu bằng RDT
    // ========================================================
    RDTSender sender(
        udp,
        serverIP,
        pasvPort);
    sender.setCancellationCallback([this]() {
        if (!m_aborted.load())
            return false;
        sendAbortWithoutWaiting();
        return true;
    });

    const std::vector<uint8_t> encoded =
        TransferModeCodec::encode(fileBuffer, m_transferMode);
    bool ok = sender.sendBuffer(encoded.data(), encoded.size());

    udp.close();

    if (!ok)
    {
        if (m_abortCommandSent.load())
        {
            const std::string aborted = recvControlReply(m_controlSock);
            const std::string completed = recvControlReply(m_controlSock);
            return openingReply + "\n" + aborted + "\n" + completed;
        }
        return "Error: RDT send failed (cwnd=" +
               std::to_string(
                   static_cast<int>(
                       sender.getFinalCwnd())) +
               ")";
    }

    std::printf(
        "[CLIENT] Transfer OK: %lld bytes | "
        "cwnd=%.2f | timeouts=%d | segs=%u\n",
        static_cast<long long>(fileSize),
        sender.getFinalCwnd(),
        sender.getTotalTimeouts(),
        sender.getTotalSegsSent());

    // ========================================================
    // 7. Đọc reply cuối THẬT
    // ========================================================
    std::string finalReply =
        recvControlReply(m_controlSock);

    if (finalReply.empty())
    {
        return openingReply +
               "\nError: Missing final server reply.";
    }

    return openingReply +
           "\n" +
           finalReply;
}

// ============================================================
// LIST / NLST
//
// LIST:
//      listing chi tiết
//
// NLST:
//      chỉ tên file / directory
//
// Cả hai đều:
//      command qua TCP
//      directory data qua UDP/RDT
// ============================================================
std::string ClientCLI::handleList(
    const std::string &path,
    bool namesOnly)
{
    // ========================================================
    // 1. PASV
    // ========================================================
    std::string pasvReply =
        m_sendCommand("PASV");

    if (pasvReply.empty() ||
        pasvReply.rfind("227", 0) != 0)
    {
        return "Error: PASV failed: " +
               pasvReply;
    }

    std::string serverIP;

    uint16_t pasvPort =
        parsePasvReply(
            pasvReply,
            serverIP);

    if (pasvPort == 0)
    {
        return "Error: Cannot parse PASV reply.";
    }

    // ========================================================
    // 2. Mở UDP socket
    // ========================================================
    UDPSocket udp;

    try
    {
        udp.open();
        udp.bind(0);
        udp.setRecvTimeout(15000);
    }
    catch (const std::exception &e)
    {
        return std::string(
                   "Error: Cannot open UDP socket: ") +
               e.what();
    }

    // ========================================================
    // 3. Knock passive UDP socket
    //
    // Server dùng datagram này để học IP + port của client.
    // ========================================================
    uint8_t knock[CUSTOM_UDP_HEADER_SIZE]{};

    udp.sendTo(
        knock,
        sizeof(knock),
        serverIP,
        pasvPort);

    // ========================================================
    // 4. Tạo command LIST / NLST
    // ========================================================
    std::string command =
        namesOnly
            ? "NLST"
            : "LIST";

    if (!path.empty())
    {
        command += " " + path;
    }

    // ========================================================
    // 5. Gửi command TCP
    // ========================================================
    std::string openingReply =
        m_sendCommand(command);

    if (openingReply.empty())
    {
        return "Error: No reply to " +
               command;
    }

    if (openingReply.rfind("150", 0) != 0)
    {
        // 425, 550,...
        return openingReply;
    }

    // ========================================================
    // 6. Nhận directory data bằng Reliable UDP
    // ========================================================
    RDTReceiver receiver(udp);
    receiver.setTimeoutMs(250);
    receiver.setCancellationCallback([this]() {
        if (!m_aborted.load())
            return false;
        sendAbortWithoutWaiting();
        return true;
    });

    std::vector<uint8_t> buffer;

    bool ok =
        receiver.receiveBuffer(buffer);

    if (!ok)
    {
        if (m_abortCommandSent.load())
        {
            const std::string aborted = recvControlReply(m_controlSock);
            const std::string completed = recvControlReply(m_controlSock);
            return openingReply + "\n" + aborted + "\n" + completed;
        }
        return openingReply +
               "\nError: Directory transfer failed.";
    }

    std::vector<uint8_t> decoded;
    std::string codecError;
    if (!TransferModeCodec::decode(buffer, m_transferMode, m_transferType,
                                   decoded, &codecError))
    {
        return openingReply + "\nError: MODE decode failed: " + codecError;
    }
    buffer = std::move(decoded);

    // ========================================================
    // 7. Convert buffer thành string
    // ========================================================
    std::string listing(
        buffer.begin(),
        buffer.end());

    // ========================================================
    // 8. Đọc 226 thật
    // ========================================================
    std::string finalReply =
        recvControlReply(m_controlSock);

    if (finalReply.empty())
    {
        finalReply =
            "Error: Missing final server reply.\n";
    }

    std::string result =
        openingReply + "\n";

    result += listing;

    /*
     * Nếu PathManager không thêm newline cuối listing
     * thì thêm để 226 không dính vào tên file.
     */
    if (!listing.empty() &&
        listing.back() != '\n')
    {
        result += "\n";
    }

    result += finalReply;

    return result;
}

// ============================================================
// notifyAbort
// ============================================================
void ClientCLI::notifyAbort()
{
    m_aborted.store(true);
}

// ============================================================
// run() — main REPL
// ============================================================
void ClientCLI::run()
{
    std::string line;

    while (true)
    {
        // ====================================================
        // Kiểm tra tín hiệu abort
        // ====================================================
        if (m_aborted.load())
        {
            std::cout
                << "\n[CLIENT] Nhan tin hieu ngat. "
                << "Dang gui ABOR...\n";

            if (!m_abortCommandSent.load())
                m_sendCommand("ABOR");

            std::cout
                << "[CLIENT] Dang gui QUIT...\n";

            m_sendCommand("QUIT");

            break;
        }

        printPrompt();

        // ====================================================
        // Đọc input
        // ====================================================
        if (!std::getline(std::cin, line))
        {
            std::cout
                << "\n[CLIENT] EOF - dang thoat...\n";

            if (!m_aborted.load())
            {
                m_sendCommand("QUIT");
            }

            break;
        }

        line = trim(line);

        if (line.empty())
        {
            continue;
        }

        const std::string verb =
            toUpperFirstWord(line);

        // ====================================================
        // RETR / GET
        // ====================================================
        if (verb == "RETR" ||
            verb == "GET")
        {
            std::istringstream ss(line);

            std::string command;
            std::string remote;
            std::string local;

            ss >> command >>
                remote >>
                local;

            if (remote.empty())
            {
                std::cout
                    << "Usage: RETR <remote_file> "
                    << "[local_file]\n";

                continue;
            }

            if (local.empty())
            {
                local = remote;
            }

            std::cout
                << handleRetr(
                       remote,
                       local)
                << "\n";

            continue;
        }

        // ====================================================
        // STOR / PUT
        // ====================================================
        if (verb == "STOR" ||
            verb == "PUT")
        {
            std::istringstream ss(line);

            std::string command;
            std::string local;
            std::string remote;

            ss >> command >>
                local >>
                remote;

            if (local.empty())
            {
                std::cout
                    << "Usage: STOR <local_file> "
                    << "[remote_file]\n";

                continue;
            }

            if (remote.empty())
            {
                remote = local;
            }

            std::cout
                << handleStor(
                       local,
                       remote,
                       false,
                       false)
                << "\n";

            continue;
        }

        // ====================================================
        // STOU
        // ====================================================
        if (verb == "STOU")
        {
            std::istringstream ss(line);

            std::string command;
            std::string local;
            std::string remote;

            ss >> command >>
                local >>
                remote;

            if (local.empty())
            {
                std::cout
                    << "Usage: STOU <local_file> "
                    << "[remote_file]\n";

                continue;
            }

            if (remote.empty())
            {
                remote = local;
            }

            std::cout
                << handleStor(
                       local,
                       remote,
                       true,
                       false)
                << "\n";

            continue;
        }

        // ====================================================
        // APPE
        // ====================================================
        if (verb == "APPE")
        {
            std::istringstream ss(line);

            std::string command;
            std::string local;
            std::string remote;

            ss >> command >>
                local >>
                remote;

            if (local.empty())
            {
                std::cout
                    << "Usage: APPE <local_file> "
                    << "[remote_file]\n";

                continue;
            }

            if (remote.empty())
            {
                remote = local;
            }

            std::cout
                << handleStor(
                       local,
                       remote,
                       false,
                       true)
                << "\n";

            continue;
        }

        // ====================================================
        // LIST / NLST
        // ====================================================
        if (verb == "LIST" ||
            verb == "NLST")
        {
            std::istringstream ss(line);

            std::string command;
            std::string path;

            ss >> command;
            ss >> path;

            const bool namesOnly =
                (verb == "NLST");

            std::cout
                << handleList(
                       path,
                       namesOnly)
                << "\n";

            continue;
        }

        // ====================================================
        // Các command bình thường chỉ cần TCP
        // ====================================================
        std::string reply =
            m_sendCommand(line);

        // Change local state only after the server accepted the command, so
        // both sides always apply the same data-channel representation.
        if (reply.rfind("200", 0) == 0)
        {
            std::istringstream stateCommand(line);
            std::string name;
            std::string argument;
            stateCommand >> name >> argument;
            for (char &ch : name)
                ch = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(ch)));
            for (char &ch : argument)
                ch = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(ch)));

            if (name == "MODE" && argument == "S")
                m_transferMode = TransferMode::Stream;
            else if (name == "MODE" && argument == "B")
                m_transferMode = TransferMode::Block;
            else if (name == "MODE" && argument == "C")
                m_transferMode = TransferMode::Compressed;
            else if (name == "TYPE" && argument == "A")
                m_transferType = TransferType::ASCII;
            else if (name == "TYPE" && argument == "I")
                m_transferType = TransferType::Binary;
        }

        std::cout
            << reply
            << "\n";

        if (verb == "QUIT")
        {
            break;
        }
    }
}

// ============================================================
// Prompt
// ============================================================
void ClientCLI::printPrompt() const
{
    std::cout
        << "ftp> "
        << std::flush;
}

// ============================================================
// trim
// ============================================================
std::string ClientCLI::trim(
    const std::string &s)
{
    size_t start =
        s.find_first_not_of(
            " \t\r\n");

    if (start == std::string::npos)
    {
        return "";
    }

    size_t end =
        s.find_last_not_of(
            " \t\r\n");

    return s.substr(
        start,
        end - start + 1);
}

// ============================================================
// Lấy command đầu tiên và chuyển sang uppercase
// ============================================================
std::string ClientCLI::toUpperFirstWord(
    const std::string &s)
{
    size_t spacePos =
        s.find(' ');

    std::string word =
        (spacePos == std::string::npos)
            ? s
            : s.substr(0, spacePos);

    for (char &c : word)
    {
        c = static_cast<char>(
            std::toupper(
                static_cast<unsigned char>(c)));
    }

    return word;
}
