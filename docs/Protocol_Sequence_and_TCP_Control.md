# Vòng đời kết nối TCP Control và UDP Data

Tài liệu này mô tả giao thức theo đúng hiện thực trong mã nguồn Hybrid-FTP. Hệ thống duy trì một kết nối TCP lâu dài để gửi lệnh/phản hồi và tạo một phiên UDP-RDT dùng một lần cho mỗi thao tác truyền dữ liệu (`LIST`, `NLST`, `RETR`, `STOR`, `STOU`, `APPE`).

> UDP là giao thức không kết nối. Vì vậy, cụm từ “mở/ngắt kết nối UDP” trong tài liệu chỉ việc tạo socket, xác định cặp địa chỉ IP/port, chạy phiên RDT và đóng socket; UDP không có bắt tay hay đóng kết nối ở tầng vận chuyển như TCP.

## 1. Biểu đồ trình tự toàn bộ vòng đời

### 1.1. Khởi tạo và duy trì kênh TCP Control

```text
CLIENT                                      SERVER
  |                                            |
  |  (Server đã socket → bind → listen 2121)  |
  |                                            |
  |--- 1. TCP connect ------------------------>|
  |    SYN → SYN-ACK → ACK                     |
  |                                            | accept(), tạo Session
  |<-- 2. 220 Service ready -------------------|
  |                                            |
  |--- 3. USER <username> -------------------->|
  |<-- 4. 331 Need password -------------------|
  |--- 5. PASS <password> -------------------->|
  |<-- 6. 230 Login successful ---------------|
  |                                            |
  |      ┌──── Lặp nhiều lệnh FTP ────┐        |
  |--- 7. COMMAND [argument] ----------------->|
  |<-- 8. xyz Reply ---------------------------|
  |      └─────────────────────────────┘        |
  |                                            |
  |--- 9. QUIT ------------------------------->|
  |<- 10. 221 Goodbye -------------------------|
  |                                            |
  |<-- 11. TCP FIN/ACK, đóng socket ---------->|
```

Nếu kết nối không kết thúc bằng `QUIT`:

| Trường hợp | Trình tự xử lý |
|---|---|
| EOF hoặc lỗi mạng | `recv()` trả `0`/lỗi → server đóng TCP → kết thúc session. |
| Client nhận `SIGINT`/`SIGTERM` | `ABOR` → server trả `426`, `226` → `QUIT` → server trả `221` → đóng TCP. |

Mỗi kết nối TCP được xử lý bởi một thread và một `SessionState` riêng. Trạng thái này giữ tình trạng đăng nhập, thư mục hiện hành, kiểu truyền, chế độ dữ liệu ACTIVE/PASSIVE và socket UDP thụ động nếu có. `QUIT` và `NOOP` được chấp nhận trước khi đăng nhập; các lệnh còn lại yêu cầu trạng thái `LoggedIn`.

### 1.2. Phiên UDP Data ở chế độ PASSIVE

Client hiện tại tự động dùng luồng PASSIVE cho `RETR`, `STOR`, `STOU`, `APPE`, `LIST` và `NLST`.

Quy ước trong biểu đồ:

- `--- TCP --->`: lệnh hoặc phản hồi trên kênh điều khiển.
- `=== UDP ===>`: DATA, ACK hoặc tín hiệu kết thúc của RDT.

#### a. Server gửi dữ liệu: `RETR`, `LIST`, `NLST`

```text
CLIENT                                      SERVER
  |                                            |
  |--- TCP: PASV ----------------------------->| Mở UDP port động
  |<-- TCP: 227 (server IP, UDP port) ----------|
  |                                            |
  |    Mở UDP socket cục bộ                     |
  |=== UDP: knock 15 byte =====================>| Học IP:port client
  |                                            |
  |--- TCP: RETR / LIST / NLST ---------------->|
  |<-- TCP: 150 Opening data connection --------|
  |                                            |
  |<== UDP: DATA(seq=0) ========================|
  |=== UDP: ACK(0, advertisedWindow) ==========>|
  |<== UDP: DATA(seq=1 ... N-1) ================|
  |=== UDP: ACK tích lũy ======================>|
  |                                            |
  |<== UDP: FIN(seq=N) =========================|
  |=== UDP: FIN + ACK(N) ======================>|
  |                                            | Đóng UDP socket
  |<-- TCP: 226 Transfer complete --------------|
  |    Đóng UDP socket                           |
  |                                            |
  |    TCP Control vẫn mở cho lệnh tiếp theo    |
```

#### b. Client gửi dữ liệu: `STOR`, `STOU`, `APPE`

```text
CLIENT                                      SERVER
  |                                            |
  |--- TCP: PASV ----------------------------->| Mở UDP port động
  |<-- TCP: 227 (server IP, UDP port) ----------|
  |                                            |
  |--- TCP: STOR / STOU / APPE ---------------->|
  |<-- TCP: 150 hoặc 125 -----------------------|
  |    Mở UDP socket cục bộ                     |
  |                                            |
  |=== UDP: DATA(seq=0 ... N-1) ===============>|
  |<== UDP: ACK tích lũy =======================|
  |                                            |
  |=== UDP: FIN(seq=N) ========================>|
  |<== UDP: FIN + ACK(N) =======================|
  |                                            | Ghi/append file
  |                                            | Tính SHA-256
  |                                            | Đóng UDP socket
  |<-- TCP: 226 Transfer complete --------------|
  |    Đóng UDP socket                           |
  |                                            |
  |    TCP Control vẫn mở cho lệnh tiếp theo    |
```

Nếu timeout, mất gói hoặc checksum sai, receiver gửi lại ACK tích lũy cũ. Sender đặt lại cửa sổ tắc nghẽn, quay `nextSeq` về `base` và gửi lại theo Go-Back-N.

Điểm khác nhau giữa hai hướng truyền là thời điểm gửi “knock”. Với `RETR`, `LIST`, `NLST`, server là RDT sender nhưng chưa biết UDP endpoint của client nên client gửi trước một datagram 15 byte. Với upload, gói `DATA` đầu tiên tự cung cấp endpoint cho server receiver, vì vậy không cần knock riêng.

### 1.3. Phiên UDP Data ở chế độ ACTIVE

Server hỗ trợ `PORT h1,h2,h3,h4,p1,p2`. Port được tính bằng `p1 × 256 + p2`.

```text
CLIENT                                      SERVER
  |                                            |
  |    Mở và bind UDP port                     |
  |--- TCP: PORT h1,h2,h3,h4,p1,p2 ---------->| Lưu IP:port
  |<-- TCP: 200 PORT successful ----------------|
  |                                            |
  |--- TCP: lệnh truyền dữ liệu --------------->| Mở UDP tạm
  |<-- TCP: 150 Opening data connection --------|
  |                                            |
  |    Nếu server gửi:                          |
  |<== UDP: DATA ===============================|
  |=== UDP: ACK ===============================>|
  |<== UDP: FIN ================================|
  |=== UDP: FIN + ACK =========================>|
  |                                            |
  |    Nếu client gửi:                          |
  |=== UDP: DATA ==============================>|
  |<== UDP: ACK ================================|
  |=== UDP: FIN ===============================>|
  |<== UDP: FIN + ACK ==========================|
  |                                            |
  |                                            | Đóng UDP tạm
  |<-- TCP: 226 Transfer complete --------------|
  |                                            |
  |    TCP Control vẫn mở cho lệnh tiếp theo    |
```

### 1.4. Trường hợp lỗi và ngắt giữa chừng

- Không có `PORT`/`PASV`, không mở được socket hoặc không nhận được knock: server trả `425`.
- RDT timeout quá số vòng retransmit, mất FINACK hoặc transfer thất bại: server trả `426`.
- Ghi file đích thất bại sau khi nhận dữ liệu: server trả `551`.
- `ABOR` đóng passive UDP socket (nếu có), đặt `dataMode = NONE`, rồi gửi lần lượt `426` và `226`. Hiện thực hiện tại là best-effort, chưa có cờ hủy đồng bộ để ngắt tức thời một RDT transfer đang chạy trong cùng thread.
- Nếu TCP Control bị đóng đột ngột, vòng `recv()` kết thúc; server đóng TCP socket và hủy `SessionState`. `unique_ptr` trong trạng thái phiên sẽ giải phóng passive UDP socket còn lại.

## 2. Giao thức Control

### 2.1. TCP Control Packet Format

TCP là byte stream nên ứng dụng không có header gói control riêng. Đơn vị bản tin ở tầng ứng dụng là một dòng kết thúc bằng CRLF.

#### 2.1.1. Cấu trúc gói lệnh từ client

```text
<COMMAND>[ SP <ARGUMENT>] CRLF
```

| Trường | Kích thước | Quy tắc |
|---|---:|---|
| `COMMAND` | Biến đổi | Tên lệnh ASCII; parser chuyển thành chữ hoa. Ví dụ `USER`, `PASV`, `RETR`. |
| `SP` | 1+ byte | Một hoặc nhiều dấu cách/tab phân tách lệnh và đối số. Không có nếu lệnh không nhận đối số. |
| `ARGUMENT` | Biến đổi | Chuỗi đối số sau lệnh; khoảng trắng hai đầu bị loại bỏ. |
| `CRLF` | 2 byte | `\r\n` (`0x0D 0x0A`). Server đọc đến `LF` và bỏ `CR`. |

Ví dụ:

```text
USER alice\r\n
TYPE I\r\n
RETR reports/data.bin\r\n
PORT 192,168,1,10,195,80\r\n
QUIT\r\n
```

Client nối `\r\n` vào từng lệnh và dùng `sendAll()` để xử lý trường hợp TCP `send()` chỉ gửi được một phần. Server đọc tối đa 511 ký tự hữu ích cho một dòng (`char line[512]`); do đó lệnh dài hơn giới hạn có thể bị tách và không nên sử dụng.

#### 2.1.2. Cấu trúc gói phản hồi từ server

Phản hồi một dòng:

```text
<CODE> SP <MESSAGE> CRLF
```

Phản hồi nhiều dòng:

```text
<CODE>-<FIRST-LINE> CRLF
<INTERMEDIATE-LINE> CRLF
<CODE> SP <LAST-LINE> CRLF
```

Trong đó `CODE` gồm đúng ba chữ số. Client nhận biết phản hồi nhiều dòng khi ký tự thứ tư là `-` và tiếp tục đọc đến dòng bắt đầu bằng cùng mã cộng dấu cách. `HELP` và `STAT` là các lệnh có thể tạo phản hồi nhiều dòng.

Ví dụ:

```text
220 Hybrid FTP service ready.\r\n
331 Username OK, need password.\r\n
227 Entering Passive Mode (127,0,0,1,195,80).\r\n
150 Opening data connection for file download.\r\n
226 Transfer complete.\r\n
```

#### 2.1.3. Ý nghĩa nhóm mã phản hồi

| Nhóm | Ý nghĩa giao thức | Hành động phía client |
|---|---|---|
| `1xx` | Phản hồi sơ bộ tích cực; thao tác đã bắt đầu nhưng chưa hoàn tất. | Tiến hành/trông chờ data transfer và đọc thêm phản hồi cuối. |
| `2xx` | Hoàn tất tích cực. | Có thể gửi lệnh tiếp theo. |
| `3xx` | Tích cực trung gian; server cần thêm thông tin/lệnh. | Gửi bước tiếp theo như `PASS` hoặc `RNTO`. |
| `4xx` | Lỗi tạm thời; thao tác không hoàn tất nhưng có thể thử lại. | Sửa trạng thái data channel hoặc thử lại transfer. |
| `5xx` | Lỗi vĩnh viễn/cú pháp/trạng thái/quyền truy cập. | Sửa lệnh, đăng nhập hoặc đường dẫn trước khi thử lại. |

#### 2.1.4. Các mã phản hồi đã cấu hình

| Mã | Bản tin/biến thể trong mã nguồn | Khi phát sinh |
|---:|---|---|
| `125` | `FILE: <uniqueName>` | `STOU` đã chọn tên duy nhất và data transfer có thể bắt đầu. |
| `150` | `Opening data connection for ...` | Bắt đầu truyền file, danh sách thư mục hoặc danh sách tên. |
| `200` | `NOOP command successful`, `Type set to A/I`, `Mode set to S`, `PORT command successful` | Lệnh thành công. |
| `211` | `Hybrid FTP server status...` / `End of status` | `STAT` không có đối số, dạng nhiều dòng. |
| `212` | Nội dung trạng thái đường dẫn | `STAT <path>` khi đối tượng là thư mục. |
| `213` | Thông tin trạng thái file, kích thước, thời gian sửa đổi hoặc `SHA-256=<hash>` | `STAT <file>`, `SIZE`, `MDTM`, `HASH`. |
| `214` | Danh sách lệnh / `Syntax: ...` | `HELP` hoặc `HELP <command>`. |
| `220` | `Hybrid FTP service ready` | Ngay sau khi server chấp nhận TCP connection. |
| `221` | `Goodbye` | `QUIT` hợp lệ; session chuẩn bị đóng TCP. |
| `226` | `Transfer complete`, `Directory transfer complete`, `Abort successful` | RDT hoàn tất hoặc `ABOR` đã xử lý best-effort. |
| `227` | `Entering Passive Mode (h1,h2,h3,h4,p1,p2)` | `PASV` mở thành công UDP socket phía server. |
| `230` | `Login successful` | `PASS` hợp lệ sau `USER`. Hiện tại server chưa kiểm tra mật khẩu thực tế. |
| `250` | Đổi/xóa thư mục, xóa file hoặc đổi tên thành công | `CWD`, `CDUP`, `RMD`, `DELE`, `RNTO`. |
| `257` | Đường dẫn hiện hành hoặc tạo thư mục thành công | `PWD`, `MKD`. |
| `331` | `Username OK, need password` | `USER` có đối số. |
| `350` | `Requested file action pending RNTO` | `RNFR` hợp lệ; chờ `RNTO`. |
| `425` | Không có/không mở được data connection, passive socket hoặc client knock | Chưa `PORT`/`PASV`, bind UDP lỗi hoặc PASSIVE không học được endpoint. |
| `426` | `Connection closed; ... transfer aborted` | RDT/data transfer thất bại hoặc `ABOR`. |
| `500` | `Invalid command` | Dòng lệnh rỗng/không parse được khi đi vào handler. |
| `501` | Thiếu/sai đối số, sai định dạng `PORT`, `TYPE` hoặc `MODE` | Lỗi cú pháp/tham số của lệnh. |
| `502` | `Command not implemented` / `Unknown command: ...` | Lệnh không được hiện thực hoặc `HELP` hỏi lệnh không tồn tại. |
| `503` | `Login with USER first` / `RNTO must be preceded by RNFR` | Sai thứ tự lệnh. |
| `530` | `Not logged in` | Dùng lệnh được bảo vệ trước khi đăng nhập. |
| `550` | Không tồn tại, bị từ chối, nằm ngoài root hoặc thao tác file/thư mục thất bại | Lỗi đường dẫn/quyền/thao tác filesystem hoặc tính hash thất bại. |
| `551` | `Failed to write file` | Đã nhận upload nhưng không ghi được file đích. |

Không có mã `4xx` hoặc `5xx` nào khác được gửi trong hiện thực hiện tại. Mã `1xx` gồm `125`, `150`; nhóm `2xx` gồm `200`, `211`–`214`, `220`, `221`, `226`, `227`, `230`, `250`, `257`; nhóm `3xx` gồm `331`, `350`; nhóm `4xx` gồm `425`, `426`; nhóm `5xx` gồm `500`, `501`, `502`, `503`, `530`, `550`, `551`.

#### 2.1.5. Quan hệ giữa phản hồi TCP và dữ liệu UDP

Một transfer thành công có hai phản hồi trên TCP:

1. `125` hoặc `150`: server đã sẵn sàng; client bắt đầu hoặc tiếp tục RDT qua UDP.
2. `226`: RDT đã nhận `FIN/FINACK`, và với upload, server đã ghi file thành công.

Client phải đọc cả hai phản hồi. Dữ liệu file/directory không được chèn vào TCP Control. Ngược lại, ACK, FIN và FINACK của RDT chỉ đi trên UDP và không thay thế các mã `150`/`226` ở tầng FTP.

### 2.2. Định dạng segment UDP-RDT liên quan

Mỗi UDP datagram RDT gồm header đóng gói 15 byte, sau đó là payload tùy chọn:

```text
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-------------------------------+-------------------------------+
|          seqNum (32)          |          ackNum (32)          |
+-------------------------------+---------------+---------------+
| payloadLen (16)| windowSize(16)| checksum (16) |  flags (8)    |
+-------------------------------+---------------+-------+-------+
|                       payload (0..MSS)                 ...     |
+---------------------------------------------------------------+
```

| Trường | Độ dài | Vai trò |
|---|---:|---|
| `seqNum` | 32 bit | Số thứ tự segment DATA; FIN dùng `N` sau segment cuối. |
| `ackNum` | 32 bit | ACK tích lũy: segment liên tục cuối cùng đã nhận. |
| `payloadLen` | 16 bit | Số byte payload. |
| `windowSize` | 16 bit | Cửa sổ sender hoặc advertised window của receiver. |
| `checksum` | 16 bit | Phát hiện lỗi header/payload. |
| `flags` | 8 bit | `SYN=0x01`, `ACK=0x02`, `DATA=0x04`, `FIN=0x08`, `NAK=0x10`. Luồng hiện tại chủ yếu dùng `DATA`, `ACK`, `FIN`, `FIN|ACK`. |

Receiver chỉ nhận DATA đúng `expectedSeq`; gói sai thứ tự hoặc sai checksum bị bỏ và ACK tích lũy cũ được gửi lại. Sender giới hạn cửa sổ hiệu dụng bằng `min(floor(cwnd), advertisedWindow, maxWindowSegments)`. Khi timeout, sender quay về `base` theo Go-Back-N; sau khi toàn bộ DATA được ACK, sender lặp FIN cho đến khi nhận `FIN|ACK` hoặc hết số lần thử.

## 3. Trình tự tham chiếu ngắn

```text
TCP connect → 220 → USER/331 → PASS/230
→ PASV/227 (hoặc PORT/200)
→ lệnh truyền/125|150
→ UDP DATA ↔ ACK ... → UDP FIN ↔ FIN|ACK
→ TCP 226
→ các lệnh khác hoặc PASV/PORT cho transfer mới
→ QUIT/221 → đóng TCP
```

## 4. Nguồn đối chiếu trong dự án

- `src/server/ServerManager.cpp`: tạo/listen/accept TCP socket và thread cho từng client.
- `src/server/Session.cpp`: vòng lặp control, parser dispatch, quản lý ACTIVE/PASSIVE và toàn bộ reply code.
- `src/client/main_client.cpp`: kết nối TCP, framing CRLF và đọc reply một/nhiều dòng.
- `src/client/ClientCLI.cpp`: orchestration PASV, UDP socket, knock, transfer và đọc reply cuối.
- `src/rdt/CustomUDPHeader.h`: cấu trúc header và cờ RDT.
- `src/rdt/SlidingWindow.cpp`: Go-Back-N, cumulative ACK, flow/congestion control và FIN/FINACK.
