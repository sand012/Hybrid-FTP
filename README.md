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

FTP_project/
├── src/
│   ├── rdt/
│   │   ├── UDPSocket.{h,cpp}          # UDP socket wrapper
│   │   ├── CustomUDPHeader.h           # Header 15-byte: seq, ack, flags, checksum
│   │   ├── CustomHeader.cpp            # Checksum, serialize/deserialize
│   │   ├── ReliableTransfer.{h,cpp}    # GBN Sender/Receiver + file API
│   │   ├── FileTransferClient.cpp      # Nghiệm thu: sender
│   │   └── FileTransferServer.cpp      # Nghiệm thu: receiver
│   ├── server/                         # FTP Server (TCP)
│   ├── client/                         # FTP Client CLI
│   └── common/                         # PathManager, CryptoHash, ...
├── test_rdt.sh                         # Script nghiệm thu tự động
└── CMakeLists.txt
```
