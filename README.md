# Hybrid-FTP — RDT + Sliding Window

Dự án FTP lai ghép TCP (control channel) và UDP+RDT (data channel) với kiểm soát luồng/tắc nghẽn bằng **Go-Back-N Sliding Window**.

---

## Kiến trúc RDT (Data Channel)

```table
┌─────────────────────────────────────────────────────────┐
│                    RDTSender / RDTReceiver               │
│  ┌─────────────────┐         ┌──────────────────────┐   │
│  │  sendFile()     │ ──GBN──▶│  receiveFile()       │   │
│  │  sendBuffer()   │         │  receiveBuffer()     │   │
│  │  sendPacket()   │ ──S&W──▶│  receivePacket()     │   │
│  └─────────────────┘         └──────────────────────┘   │
│           │                            │                 │
│    ┌──────▼──────┐             ┌───────▼──────┐          │
│    │  UDPSocket  │             │  UDPSocket   │          │
│    └─────────────┘             └──────────────┘          │
└─────────────────────────────────────────────────────────┘
```

### Thuật toán: Go-Back-N (GBN)

| Tham số | Mặc định | Mô tả |
| --------- | ---------- | ------- |
| `windowSize` | 8 | Số gói "in-flight" tối đa |
| `chunkSize` | 1400 B | Kích thước payload mỗi gói |
| `timeoutMs` | 2000 ms | Thời gian chờ ACK trước khi Go-Back |
| `maxRetries` | 10 | Số lần Go-Back tối đa |

**Cơ chế:**

- Sender gửi tối đa `windowSize` gói không chờ ACK lần lượt
- Receiver chỉ chấp nhận gói đúng thứ tự (`expectedSeq`), gửi Cumulative ACK
- Timeout → Sender quay lại `base`, gửi lại toàn bộ cửa sổ (Go-Back-N)
- Kết thúc: Sender gửi `FIN`, Receiver trả `FINACK`

---

## Build

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Các target

| Target | Mô tả |
| -------- | ------- |
| `ftp_server` | FTP Server (TCP control) |
| `ftp_client` | FTP Client CLI |
| `rdt_file_server` | Receiver — nghiệm thu file transfer |
| `rdt_file_client` | Sender — nghiệm thu file transfer |
| `rdt_saw_server` | Stop-and-Wait demo server |
| `rdt_saw_client` | Stop-and-Wait demo client |

---

## Test nghiệm thu RDT

```bash
# Chạy toàn bộ suite tự động (build + test nhiều file + nhiều window size)
bash test_rdt.sh
```

### Chạy thủ công

**Terminal 1 (receiver):**

```bash
./build/rdt_file_server 9001 output.bin
```

**Terminal 2 (sender):**

```bash
# Gửi file với window size = 16
./build/rdt_file_client 127.0.0.1 9001 myfile.bin 16
```

**Kiểm tra toàn vẹn:**

```bash
# So sánh SimpleSum được in ra ở cả hai đầu
# Hoặc dùng md5sum:
md5sum myfile.bin output.bin
```

---

## Kết quả nghiệm thu (localhost)

| File | Window | Kết quả | Throughput |
| ------ | -------- | --------- | ------------ |
| 64 B txt | 1 | ✅ | ~330 KB/s |
| 64 KB binary | 4 | ✅ | ~75 MB/s |
| 64 KB binary | 8 | ✅ | ~116 MB/s |
| 64 KB binary | 16 | ✅ | ~117 MB/s |
| 512 KB binary | 8 | ✅ | ~166 MB/s |
| 512 KB binary | 16 | ✅ | ~241 MB/s |
| 141 KB text | 8 | ✅ | ~166 MB/s |

> Throughput tăng rõ rệt khi tăng window size (hiệu quả của Sliding Window so với Stop-and-Wait).

---

## Cấu trúc thư mục

```structure
Hybrid-FTP/
├── src/
│   ├── rdt/                            # Giao thức RDT (Reliable Data Transfer) qua UDP
│   │   ├── UDPSocket.{h,cpp}           # Lớp bọc UDP socket gửi/nhận
│   │   ├── CustomUDPHeader.h           # Header của gói tin UDP tự định nghĩa (15 bytes)
│   │   ├── CustomHeader.cpp            # Xử lý tuần tự hóa (serialize/deserialize) header và checksum
│   │   ├── SlidingWindow.{h,cpp}       # Quản lý cửa sổ trượt Sliding Window (cho Go-Back-N)
│   │   ├── ReliableTransfer.{h,cpp}    # Thiết lập cơ chế truyền nhận tin cậy (RDTSender/RDTReceiver)
│   │   ├── FileTransferClient.cpp      # Client test truyền file bằng RDT
│   │   ├── FileTransferServer.cpp      # Server test nhận file bằng RDT
│   │   ├── RdtClientTest.cpp           # File kiểm thử gửi RDT đơn giản
│   │   └── RdtSeverTest.cpp            # File kiểm thử nhận RDT đơn giản
│   ├── server/                         # Mã nguồn của FTP Server (TCP Control Channel)
│   │   ├── ServerManager.{h,cpp}       # Quản lý vòng lặp và lắng nghe kết nối của Server
│   │   ├── Session.{h,cpp}             # Xử lý luồng phiên làm việc (FTP session) của từng client
│   │   └── main_server.cpp             # Điểm chạy chính của ftp_server
│   ├── client/                         # Mã nguồn của FTP Client CLI
│   │   ├── ClientCLI.{h,cpp}           # Xử lý giao diện dòng lệnh tương tác (ftp> prompt)
│   │   ├── main_client.cpp             # Điểm chạy chính của ftp_client
│   │   └── test_e2e_dev3.cpp           # File test End-to-End nâng cao
│   └── common/                         # Các lớp tiện ích chung
│       ├── CommandParser.{h,cpp}       # Parse cú pháp câu lệnh FTP từ client gửi lên
│       ├── CryptoHash.{h,cpp}          # Hỗ trợ tính mã băm SHA-256 xác thực toàn vẹn file
│       ├── FileHandler.{h,cpp}         # Tiện ích đọc ghi file cơ bản
│       ├── PathManager.{h,cpp}         # Quản lý đường dẫn, bảo mật hộp cát (sandbox) thư mục
│       ├── TCPControl.cpp              # Tiện ích kênh điều khiển TCP
│       └── test_dev3.sh                # Script test riêng cho Dev 3
├── tests/                              # Thư mục kiểm thử tự động
│   └── PathManagerListingTest.cpp      # Kiểm thử chức năng phân tách & liệt kê thư mục của PathManager
├── docs/                               # Tài liệu hướng dẫn thiết kế kỹ thuật
├── phanchiacongviec/                   # Phân chia công việc và các file checklist tiến độ
├── test_rdt.sh                         # Script nghiệm thu tự động RDT (localhost)
└── CMakeLists.txt                      # Cấu hình biên dịch CMake của dự án
```

---

## Hướng dẫn sử dụng FTP Server & Client (Hybrid TCP/UDP RDT)

Hệ thống FTP này hoạt động theo cơ chế lai ghép (Hybrid):
- **Kênh điều khiển (Control Channel):** Sử dụng giao thức TCP để gửi/nhận lệnh điều khiển FTP tiêu chuẩn.
- **Kênh truyền dữ liệu (Data Channel):** Sử dụng giao thức UDP kết hợp giải thuật RDT (Sliding Window GBN) để truyền nội dung tập tin và danh sách thư mục một cách tin cậy.

### 1. Khởi chạy FTP Server

Theo mặc định, FTP Server lắng nghe các kết nối điều khiển TCP tại cổng `2121` và lưu trữ các tệp tin trong thư mục `server_storage/`.

```bash
# Khởi chạy server tại cổng mặc định 2121
./build/ftp_server

# Hoặc khởi chạy tại một cổng chỉ định (ví dụ: 8080)
./build/ftp_server 8080
```

### 2. Khởi chạy FTP Client CLI

Khởi chạy client để kết nối tới kênh điều khiển của Server:

```bash
# Kết nối tới server ở localhost:2121
./build/ftp_client 127.0.0.1 2121
```

Sau khi kết nối thành công, bạn sẽ nhận được thông điệp chào mừng và giao diện tương tác dòng lệnh `ftp> `.

### 3. Quy trình Đăng nhập (Authentication)

Server yêu cầu đăng nhập trước khi thực hiện các lệnh liên quan tới tập tin. Bạn có thể sử dụng bất kỳ tên đăng nhập và mật khẩu nào:

```ftp
ftp> USER anonymous
331 Username OK, need password.
ftp> PASS any_password
230 Login successful.
```

### 4. Các lệnh được hỗ trợ trên Client CLI

| Lệnh CLI / FTP | Mô tả | Kênh truyền | Ví dụ sử dụng |
| :--- | :--- | :--- | :--- |
| **`USER`** | Khai báo tên người dùng | TCP Control | `USER anonymous` |
| **`PASS`** | Khai báo mật khẩu | TCP Control | `PASS 123` |
| **`PWD`** | Hiển thị đường dẫn thư mục hiện tại trên server | TCP Control | `PWD` |
| **`CWD`** | Chuyển thư mục làm việc trên server | TCP Control | `CWD docs` |
| **`CDUP`** | Di chuyển lên thư mục cha | TCP Control | `CDUP` |
| **`MKD`** | Tạo thư mục mới trên server | TCP Control | `MKD new_folder` |
| **`RMD`** | Xóa thư mục trên server | TCP Control | `RMD new_folder` |
| **`DELE`** | Xóa tập tin trên server | TCP Control | `DELE old_file.bin` |
| **`SIZE`** | Xem kích thước tập tin trên server | TCP Control | `SIZE myfile.bin` |
| **`MDTM`** | Xem thời gian chỉnh sửa cuối cùng của tập tin | TCP Control | `MDTM myfile.bin` |
| **`TYPE`** | Chọn chế độ truyền tải (`A` - ASCII, `I` - Binary) | TCP Control | `TYPE I` |
| **`MODE`** | Chọn cách biểu diễn data (`S` - Stream, `B` - Block, `C` - Compressed/RLE) | TCP Control | `MODE B` |
| **`HASH`** | Tính và trả về mã SHA-256 của file trên server | TCP Control | `HASH myfile.bin` |
| **`LIST`** | Hiển thị danh sách tệp/thư mục chi tiết | UDP (RDT) | `LIST` hoặc `LIST subfolder` |
| **`NLST`** | Hiển thị danh sách tên tệp/thư mục rút gọn | UDP (RDT) | `NLST` |
| **`RETR` / **`GET`** | Tải tập tin từ server về máy local | UDP (RDT) | `RETR remote.bin local.bin` |
| **`STOR` / **`PUT`** | Tải tập tin từ local lên server | UDP (RDT) | `STOR local.bin remote.bin` |
| **`STOU`** | Upload file với tên duy nhất không trùng lặp | UDP (RDT) | `STOU local.bin` |
| **`APPE`** | Ghi tiếp dữ liệu vào file có sẵn trên server | UDP (RDT) | `APPE local.bin remote.bin` |
| **`QUIT`** | Ngắt kết nối và thoát chương trình | TCP Control | `QUIT` |

### 5. Kịch bản mẫu Demo kiểm tra truyền file và tính toàn vẹn (E2E Transfer)

1. **Khởi chạy Server & Client**, sau đó thực hiện đăng nhập.
2. **Tải file lên server:**
   ```ftp
   ftp> PUT myfile.bin server_file.bin
   ```
3. **Kiểm tra file trên server:**
   ```ftp
   ftp> LIST
   ftp> HASH server_file.bin
   ```
4. **Tải file từ server về máy local với tên khác:**
   ```ftp
   ftp> GET server_file.bin downloaded_file.bin
   ```
5. **Thoát:**
   ```ftp
   ftp> QUIT
   ```
6. **Kiểm tra tính toàn vẹn của tệp ở phía local (ngoài Terminal):**
   ```bash
   md5sum myfile.bin downloaded_file.bin
   ```
