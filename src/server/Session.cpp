#include "Session.h"
#include "../common/CommandParser.h"
#include "ServerManager.h"
#include "../rdt/ReliableTransfer.h"
#include "../common/CryptoHash.h"
#include "../common/FileHandler.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <vector>
#include <stdexcept>

#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

/**
 * Đọc một dòng lệnh từ TCP socket.
 *
 * Dòng lệnh kết thúc bằng '\n'. Ký tự '\r' bị loại bỏ để
 * hỗ trợ đúng định dạng FTP: COMMAND argument\r\n
 *
 * @return
 *  - Số ký tự đã đọc nếu thành công.
 *  - -1 nếu client đóng kết nối hoặc xảy ra lỗi.
 */
static int read_line(int fd, char *buffer, int bufferSize) {
  if (buffer == nullptr || bufferSize <= 1) {
    return -1;
  }

  int length = 0;

  while (length < bufferSize - 1) {
    char character = '\0';

    const ssize_t receivedBytes = recv(fd, &character, 1, 0);

    if (receivedBytes == 0) {
      // Client đã đóng kết nối.
      return -1;
    }

    if (receivedBytes < 0) {
      // recv() bị ngắt bởi signal thì thử lại.
      if (errno == EINTR) {
        continue;
      }

      return -1;
    }

    if (character == '\n') {
      break;
    }

    if (character != '\r') {
      buffer[length] = character;
      ++length;
    }
  }

  buffer[length] = '\0';
  return length;
}

/**
 * Gửi toàn bộ reply về client.
 *
 * Không gọi send() đúng một lần rồi giả định toàn bộ dữ liệu
 * đã được gửi, vì send() có thể chỉ gửi được một phần.
 */
static bool send_all(int fd, const char *data, std::size_t dataSize) {
  std::size_t totalSent = 0;

  while (totalSent < dataSize) {
    const ssize_t sentBytes =
        send(fd, data + totalSent, dataSize - totalSent, 0);

    if (sentBytes < 0) {
      if (errno == EINTR) {
        continue;
      }

      return false;
    }

    if (sentBytes == 0) {
      return false;
    }

    totalSent += static_cast<std::size_t>(sentBytes);
  }

  return true;
}

/**
 * Gửi reply FTP qua TCP control channel.
 */
static bool send_reply(int fd, const std::string &reply) {
  std::printf("[Session] Reply: %s", reply.c_str());

  return send_all(fd, reply.c_str(), reply.size());
}

static std::string get_socket_ip(int fd) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, ipStr, sizeof(ipStr));
    return ipStr;
  }
  return "127.0.0.1";
}

static bool is_tcp_socket_alive(int fd) {
  char buf;
  ssize_t r = recv(fd, &buf, 1, MSG_PEEK | MSG_DONTWAIT);
  if (r == 0) {
    return false;
  }
  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return true;
    }
    return false;
  }
  return true;
}

static bool handle_help_command(int fd, const std::string &argument) {
  static const std::unordered_map<std::string, std::string> syntaxTable = {
      {"USER", "USER <username>"},
      {"PASS", "PASS <password>"},
      {"QUIT", "QUIT"},
      {"NOOP", "NOOP"},
      {"PWD", "PWD"},
      {"CWD", "CWD <path>"},
      {"CDUP", "CDUP"},
      {"MKD", "MKD <dirname>"},
      {"RMD", "RMD <dirname>"},
      {"LIST", "LIST [path]"},
      {"NLST", "NLST [path]"},
      {"STAT", "STAT [path]"},
      {"SIZE", "SIZE <filename>"},
      {"MDTM", "MDTM <filename>"},
      {"DELE", "DELE <filename>"},
      {"RNFR", "RNFR <oldname>"},
      {"RNTO", "RNTO <newname>"},
      {"TYPE", "TYPE {A | I}"},
      {"MODE", "MODE S"},
      {"PORT", "PORT <h1,h2,h3,h4,p1,p2>"},
      {"PASV", "PASV"},
      {"RETR", "RETR <filename>"},
      {"STOR", "STOR <filename>"},
      {"STOU", "STOU"},
      {"APPE", "APPE <filename>"},
      {"HASH", "HASH <filename>"},
      {"ABOR", "ABOR"},
      {"HELP", "HELP [command]"}};

  // HELP
  if (argument.empty()) {
    return send_reply(fd, "214-The following commands are recognized:\r\n"
                          " USER PASS QUIT NOOP HELP\r\n"
                          " PWD CWD CDUP MKD RMD\r\n"
                          " LIST NLST STAT SIZE MDTM\r\n"
                          " DELE RNFR RNTO\r\n"
                          " TYPE MODE PORT PASV\r\n"
                          " RETR STOR STOU APPE\r\n"
                          " HASH ABOR\r\n"
                          "214 End of HELP.\r\n");
  }

  // Chuyển argument thành chữ hoa
  std::string command = argument;

  for (char &c : command) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }

  // HELP <command>
  const auto it = syntaxTable.find(command);

  if (it != syntaxTable.end()) {
    return send_reply(fd, "214 Syntax: " + it->second + "\r\n");
  }

  // Command không tồn tại
  return send_reply(fd, "502 Unknown command: " + command + "\r\n");
}
/**
 * Xử lý một lệnh FTP.
 *
 * @return true nếu phiên làm việc cần kết thúc, ngược lại false.
 */
static bool handle_command(int fd, const std::string &line,
                           SessionState &session) {
  std::printf("[Session] Command line: %s\n", line.c_str());

  const ParsedCommand command = CommandParser::parse(line);

  if (!command.valid) {
    session.renameFrom.reset();
    send_reply(fd, "500 Invalid command.\r\n");
    return false;
  }

  /*
   * RNTO phải theo sau RNFR.
   * Lệnh khác chen vào sẽ hủy thao tác đổi tên.
   */
  if (command.name != "RNTO") {
    session.renameFrom.reset();
  }

  std::printf("[Session] Command: %s, Argument: %s\n", command.name.c_str(),
              command.argument.c_str());

  /*
   * QUIT và NOOP được phép dùng mà không cần đăng nhập.
   */

  if (command.name == "QUIT") {
    if (!command.argument.empty()) {
      send_reply(fd, "501 QUIT does not accept an argument.\r\n");
      return false;
    }

    send_reply(fd, "221 Goodbye.\r\n");
    return true;
  }

  if (command.name == "NOOP") {
    if (!command.argument.empty()) {
      send_reply(fd, "501 NOOP does not accept an argument.\r\n");
      return false;
    }

    send_reply(fd, "200 NOOP command successful.\r\n");
    return false;
  }

  /*
   * USER bắt đầu hoặc khởi động lại quá trình đăng nhập.
   */

  if (command.name == "USER") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing username.\r\n");
      return false;
    }

    session.username = command.argument;
    session.loginState = LoginState::UsernameAccepted;

    send_reply(fd, "331 Username OK, need password.\r\n");

    return false;
  }

  /*
   * PASS chỉ hợp lệ sau USER.
   */

  if (command.name == "PASS") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing password.\r\n");
      return false;
    }

    if (session.loginState != LoginState::UsernameAccepted) {
      send_reply(fd, "503 Login with USER first.\r\n");

      return false;
    }

    /*
     * Đây là xác thực cơ bản theo yêu cầu giai đoạn đầu.
     * Sau này có thể thay bằng UserRepository hoặc database.
     */
    session.loginState = LoginState::LoggedIn;

    send_reply(fd, "230 Login successful.\r\n");
    return false;
  }

  /*
   * Các lệnh từ đây trở xuống yêu cầu client đã đăng nhập.
   */

  if (session.loginState != LoginState::LoggedIn) {
    send_reply(fd, "530 Not logged in.\r\n");
    return false;
  }
  // Check Type
  if (command.name == "TYPE") {
    if (command.argument == "A" || command.argument == "a") {

      session.transferType = TransferType::ASCII;

      send_reply(fd, "200 Type set to A (ASCII).\r\n");
      return false;
    }

    if (command.argument == "I" || command.argument == "i") {

      session.transferType = TransferType::Binary;

      send_reply(fd, "200 Type set to I (Binary).\r\n");
      return false;
    }

    send_reply(fd, "501 TYPE requires A or I.\r\n");
    return false;
  }
  // Check Mode Stream
  if (command.name == "MODE") {
    if (command.argument == "S" || command.argument == "s") {

      session.transferMode = TransferMode::Stream;

      send_reply(fd, "200 Mode set to S (Stream).\r\n");
      return false;
    }

    send_reply(fd, "501 MODE only supports stream mode\r\n");
    return false;
  }
  // PORT
  if (command.name == "PORT") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing IP and port.\r\n");
      return false;
    }
    
    unsigned int h1, h2, h3, h4, p1, p2;
    if (std::sscanf(command.argument.c_str(), "%u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
      send_reply(fd, "501 Invalid PORT format.\r\n");
      return false;
    }
    
    if (h1 > 255 || h2 > 255 || h3 > 255 || h4 > 255 || p1 > 255 || p2 > 255) {
      send_reply(fd, "501 Invalid IP/port values.\r\n");
      return false;
    }
    
    char ip[64];
    std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", h1, h2, h3, h4);
    session.activeIP = ip;
    session.activePort = static_cast<uint16_t>((p1 << 8) | p2);
    session.dataMode = DataMode::ACTIVE;
    
    // Clean up passive socket if any
    if (session.passiveSocket) {
      session.passiveSocket->close();
      session.passiveSocket.reset();
    }
    
    send_reply(fd, "200 PORT command successful.\r\n");
    return false;
  }
  // PASV
  if (command.name == "PASV") {
    auto udpSock = std::make_unique<UDPSocket>();
    try {
      udpSock->open();
      udpSock->bind(0);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "[Session] Failed to open/bind UDP socket: %s\n", e.what());
      send_reply(fd, "425 Cannot open passive connection.\r\n");
      return false;
    }
    
    uint16_t port = udpSock->getLocalPort();
    std::string ip = get_socket_ip(fd);
    for (char &c : ip) {
      if (c == '.') c = ',';
    }
    
    unsigned char p1 = static_cast<unsigned char>((port >> 8) & 0xFF);
    unsigned char p2 = static_cast<unsigned char>(port & 0xFF);
    
    char replyBuf[128];
    std::snprintf(replyBuf, sizeof(replyBuf), "227 Entering Passive Mode (%s,%u,%u).\r\n", ip.c_str(), p1, p2);
    
    session.passiveSocket = std::move(udpSock);
    session.dataMode = DataMode::PASSIVE;
    
    send_reply(fd, replyBuf);
    return false;
  }
  /*
   * PWD: hiển thị thư mục hiện tại của client.
   */
  if (command.name == "PWD") {
    if (!command.argument.empty()) {
      send_reply(fd, "501 PWD does not accept an argument.\r\n");
      return false;
    }

    const std::string currentPath = session.pathManager.getCurrentFTPPath();

    send_reply(fd, "257 \"" + currentPath + "\" is the current directory.\r\n");

    return false;
  }
  if (command.name == "RMD") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing directory name.\r\n");
      return false;
    }

    if (!session.pathManager.removeDirectory(command.argument)) {
      send_reply(fd, "550 Cannot remove directory.\r\n");
      return false;
    }

    send_reply(fd, "250 Directory removed successfully.\r\n");
    return false;
  }
  /*
   * CWD: thay đổi thư mục hiện tại.
   */
  if (command.name == "CWD") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing directory path.\r\n");
      return false;
    }

    if (!session.pathManager.changeDirectory(command.argument)) {
      send_reply(fd, "550 Directory unavailable or access denied.\r\n");
      return false;
    }

    send_reply(fd, "250 Directory changed successfully.\r\n");
    return false;
  }
  // IMPLEMENT MORE
  if (command.name == "CDUP") {
    if (!command.argument.empty()) {
      send_reply(fd, "501 CDUP does not accept an argument.\r\n");
      return false;
    }

    if (!session.pathManager.changeToParentDirectory()) {
      send_reply(fd, "550 Already at root directory.\r\n");
      return false;
    }

    send_reply(fd, "250 Parent directory changed successfully.\r\n");
    return false;
  }
  if (command.name == "MKD") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing directory name.\r\n");
      return false;
    }

    if (!session.pathManager.createDirectory(command.argument)) {
      send_reply(fd, "550 Cannot create directory.\r\n");
      return false;
    }

    send_reply(fd, "257 Directory created successfully.\r\n");
    return false;
  }
  // IMPLEMENT HERE
  if (command.name == "LIST") {
    const auto listing = session.pathManager.listDirectory(command.argument);

    if (!listing.has_value()) {
      send_reply(fd, "550 Directory unavailable or access denied.\r\n");
      return false;
    }

    send_reply(fd, "150 Opening data connection for directory list.\r\n");

    // TODO Ngày 8:
    // gửi listing.value() qua UDP/RDT data channel

    send_reply(fd, "226 Directory transfer complete.\r\n");

    return false;
  }
  if (command.name == "NLST") {
    const auto listing = session.pathManager.listNames(command.argument);

    if (!listing.has_value()) {
      send_reply(fd, "550 Path unavailable or access denied.\r\n");
      return false;
    }

    send_reply(fd, "150 Opening data connection for name list.\r\n");

    // TODO Ngày 8:
    // gửi listing.value() qua UDP/RDT data channel

    send_reply(fd, "226 Directory transfer complete.\r\n");

    return false;
  }
  if (command.name == "STAT") {
    if (command.argument.empty()) {
      send_reply(fd, "211-Hybrid FTP server status:\r\n"
                     " Server is running\r\n"
                     " TYPE: " +
                         std::string(session.transferType == TransferType::ASCII
                                         ? "ASCII"
                                         : "Binary") +
                         "\r\n MODE: Stream\r\n"
                         "211 End of status.\r\n");
      return false;
    }

    const auto status = session.pathManager.getStatus(command.argument);

    if (!status.has_value()) {
      send_reply(fd, "550 Path unavailable or access denied.\r\n");
      return false;
    }

    const std::string code = status->isDirectory ? "212" : "213";
    const std::string subject = status->isDirectory ? "Directory status follows"
                                                    : "File status follows";

    send_reply(fd, code + "-" + subject + ".\r\n" + status->listing + code +
                       " End of status.\r\n");

    return false;
  }
  if (command.name == "SIZE") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    const auto size = session.pathManager.getFileSize(command.argument);

    if (!size.has_value()) {
      send_reply(fd, "550 File unavailable or access denied.\r\n");
      return false;
    }

    const std::string response = "213 " + std::to_string(size.value()) + "\r\n";

    send_reply(fd, response);
    return false;
  }
  if (command.name == "MDTM") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    const auto modificationTime =
        session.pathManager.getModificationTime(command.argument);

    if (!modificationTime.has_value()) {
      send_reply(fd, "550 File unavailable or access denied.\r\n");
      return false;
    }

    const std::string response = "213 " + modificationTime.value() + "\r\n";

    send_reply(fd, response.c_str());
    return false;
  }

  if (command.name == "HELP") {
    handle_help_command(fd, command.argument);

    return false;
  }
  if (command.name == "DELE") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    if (!session.pathManager.deleteFile(command.argument)) {
      send_reply(fd, "550 File unavailable or access denied.\r\n");
      return false;
    }

    send_reply(fd, "250 File deleted successfully.\r\n");

    return false;
  }
  if (command.name == "RNFR") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing source path.\r\n");
      return false;
    }

    if (!session.pathManager.canRename(command.argument)) {
      send_reply(fd, "550 File or directory unavailable.\r\n");
      return false;
    }

    session.renameFrom = command.argument;

    send_reply(fd, "350 Requested file action pending RNTO.\r\n");

    return false;
  }
  if (command.name == "RNTO") {
    /*
     * RNTO tiêu thụ trạng thái RNFR dù thành công
     * hay thất bại.
     */
    const auto renameSource = session.renameFrom;

    session.renameFrom.reset();

    if (command.argument.empty()) {
      send_reply(fd, "501 Missing destination path.\r\n");
      return false;
    }

    if (!renameSource.has_value()) {
      send_reply(fd, "503 RNTO must be preceded by RNFR.\r\n");
      return false;
    }

    if (!session.pathManager.renamePath(renameSource.value(),
                                        command.argument)) {
      send_reply(fd, "550 Rename failed or access denied.\r\n");
      return false;
    }

    send_reply(fd, "250 Rename successful.\r\n");

    return false;
  }
  // ============================================================
  // Mở data channel UDP theo mode ACTIVE hoặc PASSIVE.
  // Trả về true nếu thành công, false nếu lỗi.
  // Hàm sẽ thiết lập conn_sock và conn_ip/conn_port cho caller.
  // ============================================================
  auto open_data_channel =
      [&](UDPSocket *&dataSocket, std::string &peerIP, uint16_t &peerPort,
          bool &ownsSocket) -> bool {
    if (session.dataMode == DataMode::PASSIVE) {
      if (!session.passiveSocket) {
        send_reply(fd, "425 No passive socket available.\r\n");
        return false;
      }
      dataSocket = session.passiveSocket.get();
      ownsSocket = false; // session owns it
      // For passive mode, peerIP/peerPort will be learned on first receive
      peerIP = "";
      peerPort = 0;
      return true;
    } else if (session.dataMode == DataMode::ACTIVE) {
      if (session.activeIP.empty() || session.activePort == 0) {
        send_reply(fd, "425 Use PORT or PASV first.\r\n");
        return false;
      }
      // Active mode: server creates a fresh UDP socket and sends to client's IP:port
      auto *sock = new UDPSocket();
      try {
        sock->open();
        sock->bind(0);
      } catch (const std::exception &e) {
        delete sock;
        send_reply(fd, "425 Cannot open data connection.\r\n");
        return false;
      }
      dataSocket = sock;
      ownsSocket = true;
      peerIP = session.activeIP;
      peerPort = session.activePort;
      return true;
    } else {
      send_reply(fd, "425 Use PORT or PASV first.\r\n");
      return false;
    }
  };

  // ============================================================
  // RETR — Server gửi file đến client qua RDT data channel
  // ============================================================
  if (command.name == "RETR") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    const std::filesystem::path resolvedPath =
        session.pathManager.resolvePath(command.argument);

    if (!session.pathManager.isPathInsideRoot(command.argument)) {
      send_reply(fd, "550 Access denied.\r\n");
      return false;
    }

    const auto fileSize = session.pathManager.getFileSize(command.argument);
    if (!fileSize.has_value()) {
      send_reply(fd, "550 File not found or access denied.\r\n");
      return false;
    }

    UDPSocket *dataSocket = nullptr;
    std::string peerIP;
    uint16_t peerPort = 0;
    bool ownsSocket = false;

    if (!open_data_channel(dataSocket, peerIP, peerPort, ownsSocket)) {
      return false;
    }

    send_reply(fd, "150 Opening data connection for file download.\r\n");

    std::string resolvedStr = resolvedPath.string();
    // Ensure server_storage dir exists
    {
      std::error_code ec;
      std::filesystem::create_directories(resolvedPath.parent_path(), ec);
    }

    // Set peer for ACTIVE mode (PASSIVE learns peer from first receive)
    if (session.dataMode == DataMode::ACTIVE && !peerIP.empty() && peerPort != 0) {
      RDTSender sender(*dataSocket, peerIP, peerPort);
      bool ok = sender.sendFile(resolvedStr);

      if (ownsSocket) {
        dataSocket->close();
        delete dataSocket;
      }

      if (ok) {
        // Log hash
        const std::string hash = CryptoHash::computeSHA256FromFile(resolvedStr);
        std::printf("[Session] SHA-256 sau khi RETR: %s\n", hash.c_str());
        send_reply(fd, "226 Transfer complete.\r\n");
      } else {
        send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
      }
    } else if (session.dataMode == DataMode::PASSIVE) {
      // In passive mode, the server needs to know where the client
      // will receive from. We do a "learn-on-first-send" approach:
      // Send the file, and the RDT layer handles destination via
      // receivePacket for handshake, but for RETR we need the client
      // to have connected. Use file path directly with RDT.
      // We assume client has already sent a small datagram to us,
      // so we peek for the client addr first.
      {
        uint8_t peekBuf[CUSTOM_UDP_HEADER_SIZE + 64];
        std::string fromIP;
        uint16_t fromPort = 0;
        dataSocket->setRecvTimeout(10000);
        int r = dataSocket->recvFrom(peekBuf, sizeof(peekBuf), fromIP, fromPort);
        if (r < static_cast<int>(CUSTOM_UDP_HEADER_SIZE)) {
          send_reply(fd, "425 No connection from client on passive port.\r\n");
          if (ownsSocket) {
            dataSocket->close();
            delete dataSocket;
          }
          return false;
        }
        peerIP = fromIP;
        peerPort = fromPort;
      }
      RDTSender sender(*dataSocket, peerIP, peerPort);
      bool ok = sender.sendFile(resolvedStr);

      // Reset passive socket for re-use
      session.passiveSocket.reset();
      session.dataMode = DataMode::NONE;

      if (ok) {
        const std::string hash = CryptoHash::computeSHA256FromFile(resolvedStr);
        std::printf("[Session] SHA-256 sau khi RETR: %s\n", hash.c_str());
        send_reply(fd, "226 Transfer complete.\r\n");
      } else {
        send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
      }
    }

    return false;
  }

  // ============================================================
  // Helper: receive file via data channel and write to disk
  // ============================================================
  auto receiveFileOverDataChannel = [&](const std::string &destName,
                                         bool appendMode,
                                         bool skipOpeningReply = false) -> bool {
    UDPSocket *dataSocket = nullptr;
    std::string peerIP;
    uint16_t peerPort = 0;
    bool ownsSocket = false;

    if (!open_data_channel(dataSocket, peerIP, peerPort, ownsSocket)) {
      return false;
    }

    const std::filesystem::path resolvedPath =
        session.pathManager.resolvePath(destName);

    if (!session.pathManager.isPathInsideRoot(destName)) {
      send_reply(fd, "550 Access denied.\r\n");
      if (ownsSocket) {
        dataSocket->close();
        delete dataSocket;
      }
      return false;
    }

    {
      std::error_code ec;
      std::filesystem::create_directories(resolvedPath.parent_path(), ec);
    }

    if (!skipOpeningReply) {
      send_reply(fd, "150 Opening data connection for file upload.\r\n");
    }
    std::printf("[Session] Receiving -> %s (append=%d)\n", resolvedPath.c_str(),
                appendMode ? 1 : 0);

    RDTReceiver receiver(*dataSocket);
    receiver.setTimeoutMs(15000);

    if (session.dataMode == DataMode::ACTIVE) {
      // Active: receive into buffer then write
      std::vector<uint8_t> buf;
      bool ok = receiver.receiveBuffer(buf);
      if (ownsSocket) {
        dataSocket->close();
        delete dataSocket;
      }
      if (!ok) {
        send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
        return false;
      }
      bool wrote = false;
      if (session.transferType == TransferType::Binary) {
        wrote = FileHandler::writeBinaryFile(resolvedPath.string(),
                                             reinterpret_cast<const char *>(buf.data()),
                                             buf.size(), appendMode);
      } else {
        std::string text(buf.begin(), buf.end());
        wrote = FileHandler::writeTextFile(resolvedPath.string(), text, appendMode);
      }
      if (!wrote) {
        send_reply(fd, "551 Failed to write file.\r\n");
        return false;
      }
    } else {
      // Passive: receive directly to file
      std::vector<uint8_t> buf;
      bool ok = receiver.receiveBuffer(buf);
      session.passiveSocket.reset();
      session.dataMode = DataMode::NONE;
      if (!ok) {
        send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
        return false;
      }
      bool wrote = false;
      if (session.transferType == TransferType::Binary) {
        wrote = FileHandler::writeBinaryFile(resolvedPath.string(),
                                             reinterpret_cast<const char *>(buf.data()),
                                             buf.size(), appendMode);
      } else {
        std::string text(buf.begin(), buf.end());
        wrote = FileHandler::writeTextFile(resolvedPath.string(), text, appendMode);
      }
      if (!wrote) {
        send_reply(fd, "551 Failed to write file.\r\n");
        return false;
      }
    }

    const std::string hash = CryptoHash::computeSHA256FromFile(resolvedPath.string());
    std::printf("[Session] SHA-256 sau khi STOR/APPE: %s\n", hash.c_str());
    send_reply(fd, "226 Transfer complete.\r\n");
    return true;
  };

  // ============================================================
  // STOR — Client gửi file lên server (ghi đè)
  // ============================================================
  if (command.name == "STOR") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }
    receiveFileOverDataChannel(command.argument, false);
    return false;
  }

  // ============================================================
  // STOU — Giống STOR nhưng tạo tên file duy nhất
  // ============================================================
  if (command.name == "STOU") {
    const std::string baseName =
        command.argument.empty() ? "upload" : command.argument;
    const std::string uniqueName =
        session.pathManager.generateUniqueFilename(baseName);
    
    // Inform client of the unique name (RFC 959: 125 reply with filename)
    send_reply(fd, "125 FILE: " + uniqueName + "\r\n");
    // skipOpeningReply=true because we already sent 125
    receiveFileOverDataChannel(uniqueName, false, true);
    return false;
  }

  // ============================================================
  // APPE — Append to existing file (create if not exists)
  // ============================================================
  if (command.name == "APPE") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }
    receiveFileOverDataChannel(command.argument, true);
    return false;
  }

  // ============================================================
  // HASH — Compute and return SHA-256 hash of a file
  // ============================================================
  if (command.name == "HASH") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    if (!session.pathManager.isPathInsideRoot(command.argument)) {
      send_reply(fd, "550 Access denied.\r\n");
      return false;
    }

    const auto fsize = session.pathManager.getFileSize(command.argument);
    if (!fsize.has_value()) {
      send_reply(fd, "550 File not found or access denied.\r\n");
      return false;
    }

    const std::string resolvedStr =
        session.pathManager.resolvePath(command.argument).string();
    const std::string hash = CryptoHash::computeSHA256FromFile(resolvedStr);

    if (hash.empty()) {
      send_reply(fd, "550 Could not compute hash.\r\n");
      return false;
    }

    send_reply(fd, "213 SHA-256=" + hash + "\r\n");
    return false;
  }

  // ============================================================
  // ABOR — Abort current transfer (best-effort, no real abort flag here)
  // ============================================================
  if (command.name == "ABOR") {
    // Clean up any open data channel state
    if (session.passiveSocket) {
      session.passiveSocket->close();
      session.passiveSocket.reset();
    }
    session.dataMode = DataMode::NONE;
    send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
    send_reply(fd, "226 Abort successful.\r\n");
    return false;
  }

  send_reply(fd, "502 Command not implemented.\r\n");

  return false;
}

/**
 * Thread xử lý một client.
 *
 * Mỗi lần client kết nối, ServerManager tạo một SessionArgs
 * và gọi hàm này trong một thread riêng.
 */
void *handle_client_thread(void *argPtr) {
  if (argPtr == nullptr) {
    return nullptr;
  }

  SessionArgs *args = static_cast<SessionArgs *>(argPtr);

  const int fd = args->socketFd;

  /*
   * SessionState là biến cục bộ của thread.
   * Vì vậy mỗi client có loginState và username riêng.
   */
  SessionState session;

  std::printf("[Session] Client connected: %s:%d, fd=%d\n", args->ip,
              args->port, fd);

  if (!send_reply(fd, "220 Hybrid FTP service ready.\r\n")) {
    std::fprintf(stderr, "[Session] Cannot send welcome reply to fd=%d\n", fd);
  } else {
    char line[512];

    while (true) {
      const int length = read_line(fd, line, sizeof(line));

      if (length < 0) {
        std::printf("[Session] Client disconnected: fd=%d\n", fd);

        break;
      }

      // Bỏ qua dòng trống.
      if (length == 0) {
        continue;
      }

      const bool shouldQuit = handle_command(fd, line, session);

      if (shouldQuit) {
        break;
      }
    }
  }

  /*
   * Dọn dẹp tài nguyên của client.
   */

  shutdown(fd, SHUT_RDWR);
  close(fd);

  if (args->state != nullptr) {
    clients_remove(args->state, fd);
  }

  std::printf("[Session] Session ended: %s:%d, fd=%d\n", args->ip, args->port,
              fd);

  std::free(args);
  return nullptr;
}
