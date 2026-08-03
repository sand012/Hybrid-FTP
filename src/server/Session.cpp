#include "Session.h"
#include "ServerManager.h"
#include "../common/CommandParser.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
static int read_line(int fd, char* buffer, int bufferSize)
{
    if (buffer == nullptr || bufferSize <= 1) {
        return -1;
    }

    int length = 0;

    while (length < bufferSize - 1) {
        char character = '\0';

        const ssize_t receivedBytes =
            recv(fd, &character, 1, 0);

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
static bool send_all(
    int fd,
    const char* data,
    std::size_t dataSize
) {
    std::size_t totalSent = 0;

    while (totalSent < dataSize) {
        const ssize_t sentBytes = send(
            fd,
            data + totalSent,
            dataSize - totalSent,
            0
        );

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
static bool send_reply(int fd, const std::string& reply)
{
    std::printf(
        "[Session] Reply: %s",
        reply.c_str()
    );

    return send_all(fd, reply.c_str(), reply.size());
}

/**
 * Xử lý một lệnh FTP.
 *
 * @return true nếu phiên làm việc cần kết thúc, ngược lại false.
 */
static bool handle_command(
    int fd,
    const std::string& line,
    SessionState& session
) {
    std::printf(
        "[Session] Command line: %s\n",
        line.c_str()
    );

    const ParsedCommand command =
        CommandParser::parse(line);

    if (!command.valid) {
        send_reply(fd, "500 Invalid command.\r\n");
        return false;
    }

    std::printf(
        "[Session] Command: %s, Argument: %s\n",
        command.name.c_str(),
        command.argument.c_str()
    );

    /*
     * QUIT và NOOP được phép dùng mà không cần đăng nhập.
     */

    if (command.name == "QUIT") {
        if (!command.argument.empty()) {
            send_reply(
                fd,
                "501 QUIT does not accept an argument.\r\n"
            );
            return false;
        }

        send_reply(fd, "221 Goodbye.\r\n");
        return true;
    }

    if (command.name == "NOOP") {
        if (!command.argument.empty()) {
            send_reply(
                fd,
                "501 NOOP does not accept an argument.\r\n"
            );
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

        send_reply(
            fd,
            "331 Username OK, need password.\r\n"
        );

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

        if (
            session.loginState
            != LoginState::UsernameAccepted
        ) {
            send_reply(
                fd,
                "503 Login with USER first.\r\n"
            );

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

    /*
     * Những lệnh chưa tích hợp sẽ được bổ sung sau:
     *
     * PWD, CWD, CDUP, MKD, RMD
     * LIST, NLST, STAT, SIZE, MDTM
     * TYPE, MODE, PORT, PASV
     * RETR, STOR...
     */

    send_reply(
        fd,
        "502 Command not implemented.\r\n"
    );

    return false;
}

/**
 * Thread xử lý một client.
 *
 * Mỗi lần client kết nối, ServerManager tạo một SessionArgs
 * và gọi hàm này trong một thread riêng.
 */
void* handle_client_thread(void* argPtr)
{
    if (argPtr == nullptr) {
        return nullptr;
    }

    SessionArgs* args =
        static_cast<SessionArgs*>(argPtr);

    const int fd = args->socketFd;

    /*
     * SessionState là biến cục bộ của thread.
     * Vì vậy mỗi client có loginState và username riêng.
     */
    SessionState session;

    std::printf(
        "[Session] Client connected: %s:%d, fd=%d\n",
        args->ip,
        args->port,
        fd
    );

    if (!send_reply(
        fd,
        "220 Hybrid FTP service ready.\r\n"
    )) {
        std::fprintf(
            stderr,
            "[Session] Cannot send welcome reply to fd=%d\n",
            fd
        );
    }
    else {
        char line[512];

        while (true) {
            const int length =
                read_line(fd, line, sizeof(line));

            if (length < 0) {
                std::printf(
                    "[Session] Client disconnected: fd=%d\n",
                    fd
                );

                break;
            }

            // Bỏ qua dòng trống.
            if (length == 0) {
                continue;
            }

            const bool shouldQuit =
                handle_command(fd, line, session);

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

    std::printf(
        "[Session] Session ended: %s:%d, fd=%d\n",
        args->ip,
        args->port,
        fd
    );

    std::free(args);
    return nullptr;
}