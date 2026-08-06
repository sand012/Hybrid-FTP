#include "Session.h"
#include "../common/CommandParser.h"
#include "ServerManager.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#include <sys/socket.h>
#include <unistd.h>

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
  if (command.name == "RETR") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    const auto fileSize = session.pathManager.getFileSize(command.argument);

    if (!fileSize.has_value()) {
      send_reply(fd, "550 File unavailable or access denied.\r\n");
      return false;
    }

    send_reply(fd, "150 File status okay; opening data connection.\r\n");

    const bool success =
        // hàm tích hợp của Dev 2/3

        // if (!success) {
        //   send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
        //   return false;
        // }

        send_reply(fd, "226 Transfer complete.\r\n");
    return false;
  }
  if (command.name == "STOR") {
    if (command.argument.empty()) {
      send_reply(fd, "501 Missing filename.\r\n");
      return false;
    }

    send_reply(fd, "150 File status okay; opening data connection.\r\n");

    // const bool success =
    //     receiveFileOverDataChannel(session,
    //                                command.argument); // Dev 2/3

    // if (!success) {
    //   send_reply(fd, "426 Connection closed; transfer aborted.\r\n");
    //   return false;
    // }

    send_reply(fd, "226 Transfer complete.\r\n");
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
