#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "../common/TransferModeCodec.h"
#include "../rdt/UDPSocket.h"

/**
 * ClientCLI — giao diện dòng lệnh FTP client.
 *
 * Hỗ trợ các lệnh đặc biệt cần data channel:
 *   RETR <remote_file>         → tải file về local (tên giống remote)
 *   RETR <remote> <local>      → tải file về với tên local chỉ định
 *   STOR <local_file>          → upload file lên server (tên giống local)
 *   STOR <local> <remote>      → upload với tên remote chỉ định
 *
 * Các lệnh này được xử lý nội bộ bằng DataSender/DataReceiver trước
 * khi gửi lên server. Mọi lệnh khác được chuyển thẳng qua CommandSender.
 */
class ClientCLI {
public:
  std::string handleList(const std::string &path, bool namesOnly);
  // Gửi một dòng lệnh FTP raw đến server và trả về reply.
  using CommandSender =
      std::function<std::string(const std::string &commandLine)>;

  // sock: TCP control socket fd — dùng để đọc IP server cho data channel.
  ClientCLI(CommandSender sender, int controlSock);

  void run();

  /**
   * Gọi từ signal handler (SIGINT/SIGTERM) để yêu cầu CLI thoát sạch.
   * CLI sẽ gửi ABOR rồi QUIT trong lần lặp tiếp theo.
   */
  void notifyAbort();

  /** Trả về true nếu đã nhận tín hiệu abort. */
  bool isAborted() const { return m_aborted.load(); }

private:
  void printPrompt() const;
  static std::string trim(const std::string &s);
  static std::string toUpperFirstWord(const std::string &s);
  std::string handlePort(const std::string &commandLine);

  // Xử lý RETR qua socket active đã bind hoặc một socket PASV mới.
  std::string handleRetr(const std::string &remoteFile,
                         const std::string &localFile);

  // Xử lý STOR/STOU/APPE qua active hoặc passive data channel.
  std::string handleStor(const std::string &localFile,
                         const std::string &remoteFile, bool unique = false,
                         bool append = false);

  // Parse passive mode response "227 ... (h1,h2,h3,h4,p1,p2)" → trả về port
  uint16_t parsePasvReply(const std::string &reply, std::string &outIP) const;
  bool sendAbortWithoutWaiting();

  CommandSender m_sendCommand;
  int m_controlSock;                  // TCP fd — dùng để lấy server IP
  std::string m_serverHost;           // IP server (lấy từ socket hoặc argv)
  std::atomic<bool> m_aborted{false}; // set true khi nhận SIGINT/SIGTERM
  std::atomic<bool> m_abortCommandSent{false};
  TransferType m_transferType{TransferType::ASCII};
  TransferMode m_transferMode{TransferMode::Stream};
  enum class DataMode { Passive, Active };
  DataMode m_dataMode{DataMode::Passive};
  std::unique_ptr<UDPSocket> m_activeSocket;
  uint16_t m_activePort{0};
};
