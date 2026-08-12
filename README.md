# Hybrid-FTP — TCP Control & UDP RDT Sliding Window

> **Đồ án Lập trình Socket — Môn Mạng Máy Tính (Computer Networks)**  
> **Trường Đại học Khoa học Tự nhiên, ĐHQG-HCM (HCMUS)**

---

## 📌 Giới thiệu tổng quan

**Hybrid-FTP** là hệ thống truyền tải tập tin theo mô hình Client - Server lai ghép (Hybrid Architecture):

- **Kênh điều khiển (Control Channel):** Sử dụng giao thức **TCP** tuân thủ đặc tả giao thức FTP (RFC 959) nhằm trao đổi các bản tin điều khiển, xác thực, thiết lập tham số truyền và phản hồi mã trạng thái (1xx, 2xx, 3xx, 4xx, 5xx).

- **Kênh truyền dữ liệu (Data Channel):** Sử dụng giao thức **UDP** kết hợp tầng truyền tải tin cậy tự thiết kế (**Reliable Data Transfer - RDT**) với giải thuật **Sliding Window (Go-Back-N)**, tích hợp cơ chế **Kiểm soát luồng (Flow Control)** và **Kiểm soát tắc nghẽn (Congestion Control: Slow Start + AIMD)**.

Hệ thống cung cấp giao diện dòng lệnh (CLI) trực quan, hỗ trợ đa phiên kết nối đồng thời (Multi-threading), cơ chế bảo mật hộp cát đường dẫn (Path Traversal Protection) và kiểm tra tính toàn vẹn tập tin bằng mã băm **SHA-256**.

---

## 🏗️ Kiến trúc Hệ thống (System Architecture)

```
┌───────────────────────────────────────────────────────────────────────────┐
│                                CLIENT                                     │
│  ┌───────────────────────┐                     ┌───────────────────────┐  │
│  │   ClientCLI (Prompt)  │                     │   RDTWindowSender /   │  │
│  │   Command Parser      │                     │   RDTWindowReceiver   │  │
│  └──────────┬────────────┘                     └───────────┬───────────┘  │
└─────────────┼──────────────────────────────────────────────┼──────────────┘
              │ TCP (Control Channel - Port 2121)            │ UDP (Data Channel)
              │ RFC 959 Commands & 1xx-5xx Replies           │ Custom 15B Header + Sliding Window
┌─────────────┼──────────────────────────────────────────────┼──────────────┐
│  ┌──────────▼────────────┐                     ┌───────────▼───────────┐  │
│  │   ServerManager /     │                     │   RDTWindowReceiver / │  │
│  │   Session Thread      │                     │   RDTWindowSender     │  │
│  │   PathManager Sandbox │                     │   Flow/Congestion Ctrl│  │
│  └───────────────────────┘                     └───────────────────────┘  │
│                                SERVER                                     │
└───────────────────────────────────────────────────────────────────────────┘
```

### 1. Phân tách Kênh truyền (Dual-Channel Separation)
- **Control Channel (TCP):** Đảm bảo tính tin cậy tuyệt đối cho các mệnh lệnh quản lý phiên, chuyển đổi thư mục và thỏa thuận cổng truyền dữ liệu.
- **Data Channel (UDP + RDT):** Tối ưu hóa hiệu năng truyền dữ liệu khối lớn trên giao thức UDP mà vẫn đảm bảo tính tuần tự, toàn vẹn và không mất mát gói tin.

### 2. Chế độ truyền dữ liệu (Transfer Modes & Types)
- **Active Mode (`PORT`) & Passive Mode (`PASV`):** Hỗ trợ linh hoạt cả hai cơ chế thỏa thuận cổng truyền tải dữ liệu.
- **Data Representation (`TYPE`):** 
  - `TYPE A`: Dạng văn bản (ASCII Mode) có chuẩn hóa ký tự xuống dòng.
  - `TYPE I`: Dạng nhị phân nguyên bản (Binary / Image Mode).
- **Transmission Mode (`MODE`):**
  - `MODE S` (Stream): Truyền luồng byte liên tục.
  - `MODE B` (Block): Đóng gói dữ liệu theo từng block có header kích thước.
  - `MODE C` (Compressed): Nén dữ liệu theo thuật toán Run-Length Encoding (RLE) trước khi truyền.

---

## ⚡ Giao thức RDT & Cơ chế Sliding Window

### 1. Cấu trúc Custom UDP Header (15 Bytes)

Mỗi gói tin dữ liệu hoặc ACK trên kênh UDP đều được đóng gói với Header nhị phân cố định 15 bytes (`#pragma pack(push, 1)`):

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                      Sequence Number (4B)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number (4B)                 |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Payload Length (2B)     |      Window Size (rwnd) (2B)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Checksum (2B)        |   Flags (1B)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Trường (Field) | Kích thước | Mô tả chi tiết |
| :--- | :---: | :--- |
| `seqNum` | 4 bytes | Số thứ tự tuần tự của gói tin (tính theo segment index, bắt đầu từ 0) |
| `ackNum` | 4 bytes | Số thứ tự gói tin tiếp theo mà bên nhận đang chờ đợi (Cumulative ACK) |
| `payloadLen` | 2 bytes | Độ dài dữ liệu payload thực tế trong gói tin (tối đa bằng MSS) |
| `windowSize` | 2 bytes | Kích thước cửa sổ nhận còn trống (`rwnd`) dùng cho **Kiểm soát luồng** |
| `checksum` | 2 bytes | Mã kiểm tra lỗi 16-bit Internet Checksum (tính trên cả Header + Payload) |
| `flags` | 1 byte | Các cờ điều khiển: `SYN` (0x01), `ACK` (0x02), `DATA` (0x04), `FIN` (0x08), `NAK` (0x10) |

### 2. Các giải thuật truyền tin cậy

- **Go-Back-N (GBN) với Cumulative ACK:**
  - Sender cho phép gửi liên tiếp tối đa một cửa sổ gói tin mà không cần dừng lại chờ ACK cho từng gói đơn lẻ.
  - Receiver chỉ tiếp nhận các gói đến đúng thứ tự mong đợi (`expectedSeq`) và gửi phản hồi Cumulative ACK xác nhận tất cả các gói trước đó.
  - Khi xảy ra quá thời gian chờ (Timeout), Sender tự động gửi lại toàn bộ các gói tin chưa được xác nhận bắt đầu từ gói `base`.
- **Kiểm soát luồng (Flow Control):**
  - Phía Receiver liên tục cập nhật dung lượng bộ đệm còn trống và gửi về cho Sender qua trường `windowSize` trong mỗi gói ACK.
  - Sender khống chế số lượng gói đang truyền trên mạng (`in-flight`) không vượt quá `rwnd` đã quảng bá.
- **Kiểm soát tắc nghẽn (Congestion Control — TCP Reno rút gọn):**
  - **Khởi động chậm (Slow Start):** Bắt đầu với `cwnd = 1.0 MSS`. Mỗi khi nhận một ACK hợp lệ, `cwnd` tăng gấp đôi theo chu kỳ RTT cho đến khi chạm ngưỡng `ssthresh`.
  - **Tránh tắc nghẽn (Congestion Avoidance / AIMD):** Khi `cwnd >= ssthresh`, `cwnd` tăng tuyến tính (`+1.0 / cwnd` trên mỗi ACK nhận được).
  - **Xử lý mất gói / Timeout (Multiplicative Decrease):** Thiết lập lại `ssthresh = cwnd / 2`, đưa `cwnd` về `1.0 MSS` và quay lại giai đoạn Slow Start.

---

## 🛡️ Tính năng An toàn & Toàn vẹn Dữ liệu

1. **Bảo mật Sandbox (Directory Traversal Protection):**
   - Module `PathManager` kiểm soát toàn bộ các thao tác truy cập hệ thống tệp.
   - Ngăn chặn triệt để các hành vi thoát khỏi thư mục gốc (`server_storage/`) thông qua kỹ thuật leo thang thư mục (`../`, liên kết mềm hoặc ký tự nhạy cảm).
2. **Xác thực toàn vẹn bằng SHA-256 (`CryptoHash`):**
   - Tích hợp thư viện OpenSSL tính toán mã băm SHA-256 trực tiếp trên server qua lệnh `HASH`.
   - Hỗ trợ client so sánh tính toàn vẹn của tệp tin trước và sau khi truyền tải.
3. **Xử lý đa luồng (Multi-Client Concurrency):**
   - Kiến trúc multi-threaded độc lập cho từng phiên làm việc của Client, bảo vệ trạng thái phiên và tránh nghẽn I/O.

---

## 📋 Danh sách Lệnh FTP Hỗ trợ (Command Reference)

| Nhóm chức năng | Lệnh | Kênh truyền | Cú pháp | Mô tả chi tiết |
| :--- | :--- | :---: | :--- | :--- |
| **Xác thực & Phiên** | `USER` | TCP | `USER <username>` | Khai báo tên tài khoản người dùng |
| | `PASS` | TCP | `PASS <password>` | Khai báo mật khẩu xác thực phiên |
| | `QUIT` | TCP | `QUIT` | Đóng phiên làm việc và ngắt kết nối an toàn |
| | `NOOP` | TCP | `NOOP` | Kiểm tra trạng thái hoạt động (Keep-alive) |
| | `HELP` | TCP | `HELP [command]` | Hiển thị thông tin trợ giúp lệnh |
| | `STAT` | TCP | `STAT [path]` | Xem trạng thái kết nối / thông tin đường dẫn |
| | `ABOR` | TCP | `ABOR` | Hủy bỏ tác vụ truyền nhận dữ liệu đang thực thi |
| **Cấu hình truyền tải** | `TYPE` | TCP | `TYPE <A\|I>` | Đặt kiểu truyền: ASCII (`A`) hoặc Binary (`I`) |
| | `MODE` | TCP | `MODE <S\|B\|C>` | Đặt chế độ: Stream (`S`), Block (`B`), Compressed (`C`) |
| | `PASV` | TCP | `PASV` | Yêu cầu server mở cổng lắng nghe Passive Data Channel |
| | `PORT` | TCP | `PORT <h1,h2,h3,h4,p1,p2>` | Chỉ định địa chỉ IP và Port của Client cho Active Data Channel |
| **Quản lý Thư mục** | `PWD` | TCP | `PWD` | In đường dẫn thư mục làm việc hiện tại trên server |
| | `CWD` | TCP | `CWD <dir>` | Chuyển đến thư mục làm việc chỉ định |
| | `CDUP` | TCP | `CDUP` | Di chuyển lên thư mục cha |
| | `MKD` | TCP | `MKD <dir>` | Tạo thư mục mới trên server |
| | `RMD` | TCP | `RMD <dir>` | Xóa thư mục rỗng trên server |
| **Thao tác Tệp tin** | `SIZE` | TCP | `SIZE <file>` | Lấy kích thước tệp tin tính theo byte |
| | `MDTM` | TCP | `MDTM <file>` | Lấy thời gian chỉnh sửa cuối cùng (định dạng YYYYMMDDhhmmss) |
| | `DELE` | TCP | `DELE <file>` | Xóa tệp tin trên server |
| | `RNFR` | TCP | `RNFR <file>` | Chỉ định tệp/thư mục nguồn cần đổi tên |
| | `RNTO` | TCP | `RNTO <file>` | Chỉ định tên đích mới để hoàn tất đổi tên |
| | `HASH` | TCP | `HASH <file>` | Tính và trả về mã băm SHA-256 của tệp tin trên server |
| **Truyền nhận Dữ liệu** | `LIST` | UDP (RDT) | `LIST [path]` | Liệt kê chi tiết danh sách tệp và thư mục (dạng `ls -l`) |
| | `NLST` | UDP (RDT) | `NLST [path]` | Liệt kê danh sách tên tệp rút gọn |
| | `RETR` / `GET` | UDP (RDT) | `RETR <remote> [local]` | Tải tệp tin từ Server về máy cục bộ |
| | `STOR` / `PUT` | UDP (RDT) | `STOR <local> [remote]` | Tải tệp tin từ máy cục bộ lên Server |
| | `STOU` | UDP (RDT) | `STOU <local>` | Tải tệp lên server với tên tự sinh duy nhất (tránh ghi đè) |
| | `APPE` | UDP (RDT) | `APPE <local> <remote>` | Ghi nối (append) nội dung tệp vào tệp có sẵn trên server |

---

## 💻 Yêu cầu Môi trường & Hướng dẫn Biên dịch

### 1. Yêu cầu hệ thống
- **Hệ điều hành:** Linux (Ubuntu 20.04+, Debian, WSL2, Arch Linux,...)
- **Trình biên dịch:** GCC / G++ hỗ trợ chuẩn **C++23** (g++-13 hoặc g++-14 khuyến nghị) hoặc Clang 16+
- **Công cụ xây dựng:** CMake >= 3.10, Make hoặc Ninja
- **Thư viện phụ thuộc:** OpenSSL (`libssl-dev`), POSIX Threads (`pthread`)

Cài đặt gói phụ thuộc trên Ubuntu/Debian:
```bash
sudo apt update
sudo apt install -y build-essential cmake libssl-dev
```

### 2. Quy trình biên dịch (Build)

```bash
# 1. Tạo thư mục build
mkdir -p build && cd build

# 2. Sinh cấu hình CMake chế độ Release
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. Tiến hành biên dịch toàn bộ các target
cmake --build . -j$(nproc)
```

### 3. Danh sách các Target thực thi

| Target Binary | Mô tả |
| :--- | :--- |
| **`ftp_server`** | Chương trình Server FTP chính (TCP Control + UDP RDT Data) |
| **`ftp_client`** | Giao diện dòng lệnh tương tác FTP Client CLI |
| **`rdt_file_server`** | Ứng dụng Server độc lập để benchmark và nghiệm thu module RDT |
| **`rdt_file_client`** | Ứng dụng Client độc lập để benchmark và nghiệm thu module RDT |
| **`path_manager_listing_test`** | Bộ kiểm thử tự động cho module sandbox và liệt kê thư mục `PathManager` |
| **`transfer_mode_codec_test`** | Bộ kiểm thử tự động cho bộ mã hoá `TYPE` / `MODE` (ASCII/Binary/RLE) |

---

## 🚀 Hướng dẫn Sử dụng (Usage Guide)

### 1. Khởi chạy FTP Server

Theo mặc định, Server lắng nghe kênh TCP tại cổng `2121` và quản lý tệp tại thư mục `./server_storage/`:

```bash
# Chạy với cổng mặc định 2121
./build/ftp_server

# Hoặc chỉ định cổng tùy ý (ví dụ: 8080)
./build/ftp_server 8080
```

### 2. Khởi chạy FTP Client

Mở cửa sổ Terminal mới và kết nối đến Server:

```bash
# Cú pháp: ./build/ftp_client <server_ip> [server_port]
./build/ftp_client 127.0.0.1 2121
```

---

## 📖 Kịch bản Trình diễn Mẫu (Demo Scenarios)

### Kịch bản 1: Đăng nhập và Quản trị thư mục
```ftp
# Kết nối thành công, nhận banner 220
220 Hybrid FTP Server ready.

# Thực hiện đăng nhập
ftp> USER anonymous
331 Username OK, need password.
ftp> PASS 123456
230 Login successful.

# Kiểm tra và tạo thư mục
ftp> PWD
257 "/" is current directory.
ftp> MKD my_documents
257 "my_documents" directory created.
ftp> CWD my_documents
250 Directory successfully changed to /my_documents.
ftp> PWD
257 "/my_documents" is current directory.
ftp> CDUP
250 Directory successfully changed to /.
```

### Kịch bản 2: Upload, Download và Kiểm tra Toàn vẹn (Integrity Check)
```ftp
# 1. Tải tệp tin lên server (sử dụng lệnh STOR hoặc PUT)
ftp> PUT sample_data.bin remote_data.bin
227 Entering Passive Mode (127,0,0,1,142,35).
150 Opening BINARY mode UDP data connection for STOR remote_data.bin
226 Transfer complete.

# 2. Liệt kê danh sách tệp trên server
ftp> LIST
227 Entering Passive Mode (127,0,0,1,150,22).
150 Opening BINARY mode UDP data connection for LIST.
-rw-r--r-- 1 ftp ftp 1048576 Aug 12 19:30 remote_data.bin
226 Transfer complete.

# 3. Lấy mã băm SHA-256 từ phía Server
ftp> HASH remote_data.bin
213 SHA-256 8f4b23...a91c

# 4. Tải tệp tin về máy cục bộ với tên khác (sử dụng RETR hoặc GET)
ftp> GET remote_data.bin downloaded_data.bin
227 Entering Passive Mode (127,0,0,1,162,11).
150 Opening BINARY mode UDP data connection for RETR remote_data.bin
226 Transfer complete.

# 5. Thoát phiên làm việc
ftp> QUIT
221 Goodbye.
```

Kiểm tra mã băm bên ngoài Terminal máy cục bộ:
```bash
sha256sum sample_data.bin downloaded_data.bin
# Kết quả hiển thị trùng khớp 100%
```

### Kịch bản 3: Upload an toàn không trùng tên (`STOU`) & Ghi nối tiếp (`APPE`)
```ftp
# Upload tự sinh tên duy nhất không sợ ghi đè file có sẵn:
ftp> STOU local_file.txt
227 Entering Passive Mode (127,0,0,1,170,99).
150 Opening BINARY mode UDP data connection for STOU.
226 Transfer complete. Unique file name: upload_20260812_193500_1234.txt

# Ghi nối thêm dữ liệu vào tệp hiện hữu:
ftp> APPE append_chunk.txt target_file.txt
227 Entering Passive Mode (127,0,0,1,171,12).
150 Opening BINARY mode UDP data connection for APPE target_file.txt
226 Transfer complete.
```

---

## 🧪 Kiểm thử Tự động & Đánh giá Hiệu năng (Benchmarking)

### 1. Chạy toàn bộ Test Suite RDT tự động

Dự án cung cấp kịch bản kiểm thử toàn diện `test_rdt.sh` kiểm tra nhiều kích thước tệp (từ vài chục Byte đến hàng trăm MB) và nhiều kích cỡ cửa sổ Sliding Window:

```bash
bash test_rdt.sh
```

### 2. Chạy kiểm thử tự động với CTest
```bash
cd build
ctest --output-on-failure
```

### 3. Kết quả đo đạc Thông lượng thực tế (Localhost Benchmark)

| Kích thước tệp | Cấu hình Window | Kết quả toàn vẹn | Throughput trung bình |
| :--- | :---: | :---: | :---: |
| **64 B** (Small Text) | 1 (Stop-and-Wait) | ✅ Khớp 100% | ~350 KB/s |
| **64 KB** (Binary) | 4 | ✅ Khớp 100% | ~75 MB/s |
| **64 KB** (Binary) | 8 | ✅ Khớp 100% | ~116 MB/s |
| **64 KB** (Binary) | 16 | ✅ Khớp 100% | ~120 MB/s |
| **512 KB** (Binary) | 8 | ✅ Khớp 100% | ~168 MB/s |
| **512 KB** (Binary) | 16 | ✅ Khớp 100% | ~245 MB/s |
| **5 MB** (Large Binary)| 32 (Max Window) | ✅ Khớp 100% | ~310 MB/s |

> **Nhận xét:** Khi áp dụng kỹ thuật Sliding Window kết hợp Go-Back-N và nâng kích thước cửa sổ, thông lượng đường truyền tăng trưởng vượt bậc so với cơ chế Stop-and-Wait truyền thống nhờ loại bỏ thời gian chết (dead time) chờ đợi ACK giữa các chặng RTT.

---

## 📁 Cấu trúc Thư mục Dự án

```
Hybrid-FTP/
├── CMakeLists.txt                      # Cấu hình biên dịch chính của CMake
├── README.md                           # Tài liệu tổng quan và hướng dẫn sử dụng dự án
├── test_rdt.sh                         # Script kiểm thử nghiệm thu tự động RDT
├── docs/                               # Tài liệu thiết kế và hình ảnh minh họa
│   └── image/
│       └── Section7_AppDemoEvidence/   # Ảnh chụp minh chứng nghiệm thu thực tế
├── phanchiacongviec/                   # Bảng phân công nhiệm vụ và checklist tiến độ
│   ├── Checklist_stage3.md             # Checklist hoàn thành Giai đoạn 3
│   ├── Checklist_stage4.md             # Kế hoạch báo cáo & diễn tập bảo vệ Giai đoạn 4
│   └── KeHoach_VanDap_LiveCoding.md    # Tài liệu ôn tập lý thuyết & live coding
├── server_storage/                     # Thư mục gốc lưu trữ tệp trên Server (Sandbox)
├── src/
│   ├── rdt/                            # Module truyền dữ liệu tin cậy RDT qua UDP
│   │   ├── CustomUDPHeader.h           # Định nghĩa cấu trúc Header gói tin 15 bytes & Flags
│   │   ├── CustomUDPHeader.cpp         # Xử lý tuần tự hóa (serialize) và tính checksum
│   │   ├── SlidingWindow.h             # Quản lý luồng GBN, Flow Control (rwnd) & AIMD
│   │   ├── SlidingWindow.cpp           # Cài đặt chi tiết RDTWindowSender / RDTWindowReceiver
│   │   ├── ReliableTransfer.h          # Lớp giao diện wrapper truyền tệp/buffer
│   │   ├── ReliableTransfer.cpp        # Định nghĩa các hàm truyền tải bậc cao
│   │   ├── UDPSocket.h                 # Lớp bao bọc (wrapper) socket UDP POSIX
│   │   ├── UDPSocket.cpp               # Cài đặt socket UDP gửi/nhận không đồng bộ
│   │   ├── FileTransferServer.cpp      # Chương trình Server nghiệm thu truyền tệp RDT
│   │   └── FileTransferClient.cpp      # Chương trình Client nghiệm thu truyền tệp RDT
│   ├── server/                         # Module FTP Server (Kênh điều khiển TCP)
│   │   ├── ServerManager.h             # Quản lý vòng lặp lắng nghe kết nối server
│   │   ├── ServerManager.cpp           # Thiết lập socket server TCP và dispatch luồng
│   │   ├── Session.h                   # Quản lý ngữ cảnh và trạng thái phiên của Client
│   │   ├── Session.cpp                 # Xử lý tất cả các câu lệnh FTP RFC 959
│   │   └── main_server.cpp             # Điểm vào chính của ứng dụng `ftp_server`
│   ├── client/                         # Module FTP Client (Giao diện CLI)
│   │   ├── ClientCLI.h                 # Định nghĩa lớp tương tác dòng lệnh Client
│   │   ├── ClientCLI.cpp               # Xử lý tương tác người dùng, parse lệnh và gọi RDT
│   │   └── main_client.cpp             # Điểm vào chính của ứng dụng `ftp_client`
│   └── common/                         # Các thư viện tiện ích dùng chung
│       ├── CommandParser.h             # Định nghĩa bộ phân tích cú pháp lệnh FTP
│       ├── CommandParser.cpp           # Tách tên lệnh và tham số câu lệnh
│       ├── CryptoHash.h                # Khai báo hàm băm SHA-256
│       ├── CryptoHash.cpp              # Cài đặt băm SHA-256 sử dụng OpenSSL
│       ├── FileHandler.h               # Tiện ích đọc/ghi dữ liệu tệp tin an toàn
│       ├── FileHandler.cpp             # Cài đặt thao tác nhị phân với tệp tin
│       ├── PathManager.h               # Khai báo module quản lý đường dẫn và sandbox
│       ├── PathManager.cpp             # Chống Directory Traversal và chuẩn hóa đường dẫn
│       ├── TransferModeCodec.h         # Khai báo bộ mã hóa TYPE (ASCII/Binary) & MODE (Stream/Block/RLE)
│       └── TransferModeCodec.cpp       # Cài đặt nén/giải nén RLE và chuyển đổi mã dòng
└── tests/                              # Thư mục chứa các ca kiểm thử tự động
    ├── PathManagerListingTest.cpp      # Kiểm thử unit test cho PathManager
    └── TransferModeCodecTest.cpp       # Kiểm thử unit test cho TransferModeCodec
```

---

## 👥 Nhóm Tác Giả & Phân công Công việc

- **Dev 1:** Kênh điều khiển TCP Control, Phân tích lệnh FTP (`CommandParser`), Quản lý phiên làm việc (`Session`), Kiến trúc giao thức tổng thể.
- **Dev 2:** Giao thức truyền dữ liệu tin cậy RDT trên nền UDP (`CustomUDPHeader`), Thuật toán Sliding Window (Go-Back-N), Kiểm soát luồng (`Flow Control`) & Kiểm soát tắc nghẽn (`Congestion Control: Slow Start & AIMD`).
- **Dev 3:** Quản trị tệp tin an toàn (`PathManager` Sandbox), Mã băm toàn vẹn SHA-256 (`CryptoHash`), Bộ mã hóa kiểu & chế độ (`TransferModeCodec`), Tương tác giao diện dòng lệnh Client CLI (`ClientCLI`), Test Suite tự động & Đo đạc hiệu năng.

---

## 📜 Giấy phép (License)
Dự án được xây dựng phục vụ mục đích học tập và nghiên cứu trong khuôn khổ môn học Mạng Máy Tính tại HCMUS.
