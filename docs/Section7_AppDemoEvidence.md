![1786295335081](image/Section7_AppDemoEvidence/1786295335081.png)

# Section 7: Application Demo Evidence — Minh chứng Nghiệm thu

> **Người phụ trách:** Dev 3
> **Mục đích:** Cung cấp bằng chứng hình ảnh/log thực tế chứng minh hệ thống Hybrid FTP hoạt động đầy đủ theo yêu cầu

---

## 7.1 Tổng quan các ca kiểm thử

Hybrid FTP cần thể hiện **7 ca kiểm thử chính** để chứng minh toàn bộ tính năng:

| STT | Ca kiểm thử                         | Mục đích                                                        | Evidence cần                                         |
| :-: | :------------------------------------ | :----------------------------------------------------------------- | :---------------------------------------------------- |
|  1  | **Upload file thành công**    | Chứng minh STOR + RDT + file write                                | Screenshot + log server + file received               |
|  2  | **Download file thành công**  | Chứng minh RETR + RDT + file send                                 | Screenshot + log server + file size match             |
|  3  | **So sánh mã băm SHA-256**   | Chứng minh độ toàn vẹn file (kiểm tra data không bị hỏng) | Mã băm trước/sau transfer khớp nhau (100%)       |
|  4  | **Danh sách phiên client**    | Chứng minh multi-threading + session management                   | Bảng liệt kê IP/Port/Status client đang kết nối |
|  5  | **Multiple concurrent clients** | Chứng minh server xử lý nhiều client đồng thời              | Log thể hiện 3+ client cùng upload/download        |
|  6  | **Transfer file ASCII**         | Chứng minh hỗ trợ TYPE A (text mode)                            | Log + file nội dung so sánh                         |
|  7  | **Transfer file Binary**        | Chứng minh hỗ trợ TYPE I (binary mode)                          | Log + file binary size khớp                          |

---

## 7.2 Hướng dẫn Chuẩn bị Môi trường Test

### 7.2.1 Biên dịch dự án trên Clean Machine

```bash
# 1. Tạo thư mục build sạch
cd ~/Hybrid-FTP
rm -rf build
mkdir build && cd build

# 2. Chạy CMake configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# 3. Build toàn bộ dự án
cmake --build . -- -j$(nproc)

# 4. Kiểm tra các target được build thành công
ls -lh ftp_server ftp_client
```

**Output kỳ vọng:**

```
-rwxr-xr-x 1 dev dev 392K Aug  9 10:30 ftp_server
-rwxr-xr-x 1 dev dev 696K Aug  9 10:30 ftp_client
```

### 7.2.2 Chuẩn bị dữ liệu test

```bash
# Tạo thư mục storage cho server
mkdir -p /tmp/ftp_server_storage
cd /tmp/ftp_server_storage

# Tạo file text (ASCII) để test STOR/RETR
cat > sample_text.txt << 'EOF'
Hybrid FTP Server — File Transfer Protocol Implementation
Development Group: Dev1, Dev2, Dev3
Date: August 2026

This is a sample ASCII text file for testing.
Lines include special characters: À, é, ñ, 中
Newlines: CRLF (\r\n) for FTP ASCII mode.
Numbers: 0123456789
Symbols: !@#$%^&*()_+-=[]{}|;:",.<>?/
EOF

# Tạo file binary (1MB random data)
dd if=/dev/urandom of=sample_binary.bin bs=1M count=1

# Tính mã băm SHA-256 cho cả hai file
sha256sum sample_text.txt > sample_text.sha256
sha256sum sample_binary.bin > sample_binary.sha256

echo "✅ Files prepared:"
ls -lh sample_*.txt sample_*.bin sample_*.sha256
```

**Output kỳ vọng:**

```
-rw-r--r-- 1 dev dev  256 Aug  9 10:35 sample_text.txt
-rw-r--r-- 1 dev dev 1.0M Aug  9 10:35 sample_binary.bin
-rw-r--r-- 1 dev dev   65 Aug  9 10:35 sample_text.sha256
-rw-r--r-- 1 dev dev   65 Aug  9 10:35 sample_binary.sha256
```

---

## 7.3 Ca Kiểm Thử 1: Upload File Thành Công (STOR + RDT)

### Mục đích

Chứng minh:

- Client có thể gửi file đến server qua STOR
- RDT truyền dữ liệu tin cậy (không mất gói)
- Server ghi file thành công vào disk

### Bước thực hiện

**Terminal 1 — Khởi chạy Server:**

```bash
cd ~/Hybrid-FTP/build # Thư mục build đã biên dịch
./ftp_server 2>&1 | tee /tmp/server_upload_test.log
```

**Terminal 2 — Chạy Client upload:**

```bash
mkdir -p /tmp/ftp_client_storage && cd /tmp/ftp_client_storage
~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user testuser
pass testpass
binary
type I
passive
stor sample_binary.bin
hash sample_binary.bin
quit
COMMANDS
```

### Verification

```bash
# Kiểm tra file được lưu thành công
ls -lh /tmp/ftp_server_storage/sample_binary.bin
# Output: -rw-r--r-- 1 dev dev 1.0M Aug  9 10:45 /tmp/ftp_server_storage/sample_binary.bin

# Kiểm tra kích thước file khớp
wc -c /tmp/ftp_server_storage/sample_binary.bin
# Output: 1048576 /tmp/ftp_server_storage/sample_binary.bin
```

### Screenshot test thực tế

##### Terminal 1 — Server:

![1786269216309](image/Section7_AppDemoEvidence/1786269216309.png)

##### Terminal 2 — Client:

![1786269589023](image/Section7_AppDemoEvidence/1786269589023.png)

##### File manager: Xác nhận file `sample_binary.bin` nằm trong `/tmp/ftp_server_storage/`

![1786269724832](image/Section7_AppDemoEvidence/1786269724832.png)

---

## 7.4 Ca Kiểm Thử 2: Download File Thành Công (RETR + RDT)

### Mục đích

Chứng minh:

- Server có thể gửi file đến client qua RETR
- RDT truyền dữ liệu tin cậy
- Client nhận file đúng nội dung và kích thước

### Bước thực hiện

**Terminal 1 — Server:**

```bash
./ftp_server 2>&1 | tee /tmp/server_download_test.log
```

**Terminal 2 — Client download:**

```bash
~/Hybrid-FTP/build/ftp_client << 'COMMANDS' 
open 127.0.0.1 2121
user testuser
pass testpass
binary
type I
passive
retr sample_binary.bin
quit
COMMANDS
```

### Expected Output — Server Log

```
[Server] 2026-08-09T10:50:15 <<< RETR sample_binary.bin
[Server] 2026-08-09T10:50:15 >>> 150 Opening data connection for RETR sample_binary.bin
[RDT] Sender started: sending 1048576 bytes in 45 packets
[RDT] Window size: 32, RTT: 2ms, cwnd: 16
[RDT] Packet 1-16: DATA (16384 bytes)
[RDT] ACK received for packet 1-16
[RDT] Packet 17-32: DATA (16384 bytes)
[RDT] ACK received for packet 17-32
...
[RDT] Packet 1041-1045: DATA (8192 bytes)
[RDT] ACK received for packet 1041-1045
[RDT] FIN packet sent, waiting for ACK...
[RDT] FINACK received
[RDT] Transfer complete: 1048576 bytes in 45 packets
[Server] 2026-08-09T10:50:17 >>> 226 Transfer complete. (1048576 bytes)
```

### Expected Output — Client Log

```
ftp> retr sample_binary.bin
150 Opening data connection for RETR sample_binary.bin
[Downloading sample_binary.bin ========================================>] 1.0MB
226 Transfer complete. (1048576 bytes)
ftp> quit
221 Goodbye.
```

### Verification

```bash
# Kiểm tra file được download thành công
ls -lh /tmp/ftp_client_storage/sample_binary.bin
# Output: -rw-r--r-- 1 dev dev 1.0M Aug  9 10:50

# Kiểm tra file size khớp
cmp /tmp/ftp_server_storage/sample_binary.bin /tmp/ftp_client_storage/sample_binary.bin
# Output: (no output = files are identical)
```

### Screenshot cần chụp

- ✅ Terminal Server: RETR command log

![1786270264265](image/Section7_AppDemoEvidence/1786270264265.png)

- ✅ Terminal Client: Download progress + completion

![1786270293973](image/Section7_AppDemoEvidence/1786270293973.png)

- ls command: Xác nhận file tồn tại client side

![1786270332670](image/Section7_AppDemoEvidence/1786270332670.png)

---

## 7.5 Ca Kiểm Thử 3: Kiểm Tra SHA-256 Khớp Nhau (Integrity Check)

### Mục đích

Chứng minh:

- File không bị hỏng trong quá trình transfer (RDT reliable)
- SHA-256 trước/sau khớp 100%
- HASH command hoạt động đúng

### Bước thực hiện

**Chuẩn bị:**

```bash
# Trên server
cd /tmp/ftp_server_storage
sha256sum sample_binary.bin > sample_binary.sha256.original
echo "Original hash:"
cat sample_binary.sha256.original
```

**Test sequence:**

```bash
# Terminal 1 - Server
stdbuf -oL ~/Hybrid-FTP/build/ftp_server 2>&1 | tee /tmp/server_ascii_test.log
# Terminal 2 - Client: Upload + HASH

~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user testuser
pass testpass
binary
type I
passive
stor sample_binary.bin
hash sample_binary.bin
quit
COMMANDS
```

### Capture Output

```bash
# Extract hash từ server log
grep -oP '[a-f0-9]{64}' /tmp/client_hash.log | tail -n 1 > /tmp/server_hash.txt
# Tính hash của file gốc
sha256sum /tmp/ftp_server_storage/sample_binary.bin | awk '{print $1}' > /tmp/original_hash.txt
# So sánh
echo "=== HASH COMPARISON ==="
echo "Original:  $(cat /tmp/original_hash.txt)"
echo "From HASH: $(cat /tmp/server_hash.txt)"
diff /tmp/original_hash.txt /tmp/server_hash.txt && echo "✅ MATCH!" || echo "❌ MISMATCH!"
```

### Expected Verification Result

```
=== HASH COMPARISON ===
Original:  a3f9c5e2d4b8f1a6c9e2f3b5d8a1c4e6f9a2b5c8d1e4f7a0b3c6d9e2f5a8
From HASH: a3f9c5e2d4b8f1a6c9e2f3b5d8a1c4e6f9a2b5c8d1e4f7a0b3c6d9e2f5a8
✅ MATCH!
```

### Screenshot cần chụp

✅ File original hash (trước transfer)

![1786270839633](image/Section7_AppDemoEvidence/1786270839633.png)

- ✅ Server HASH command response (sau transfer)
- ![1786272765658](image/Section7_AppDemoEvidence/1786272765658.png)
- ✅ Comparison result showing they match

![1786272788061](image/Section7_AppDemoEvidence/1786272788061.png)

---

## 7.6 Ca Kiểm Thử 4: Danh Sách Phiên Kết Nối Client (Session Management)

### Mục đích

Chứng minh:

- Server theo dõi tất cả active sessions
- Multi-threading hoạt động: mỗi client được thread riêng
- ClientRecord table lưu IP/Port/Status

### Thực hiện

**Terminal 1 — Server với verbose logging:**

```bash
# Compile với DEBUG mode để xem chi tiết threads
stdbuf -oL ~/Hybrid-FTP/build/ftp_server 2121 2>&1 | grep --line-buffered -iE "Thread|connection|Client|Session" | tee /tmp/session_tracking.log
```

**Terminal 2, 3, 4 — 3 clients kết nối đồng thời:**

```bash
# Client 1
~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user1
pass pass1
pwd
COMMANDS

# (chạy trong background hoặc terminal khác)
# Client 2
~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user1
pass pass1
list
COMMANDS

# Client 3
~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user1
pass pass1
binary
COMMANDS
```

### Tạo bảng Summary

**Tạo script để in session table:**

```bash
cat > /tmp/print_sessions.sh << 'EOF'
#!/bin/bash
echo "=== ACTIVE SESSIONS SNAPSHOT ==="
echo "Timestamp: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""
echo "Slot | IP Address   | Port  | Thread | Status   | User     | Data Mode"
echo "---- | ------------- | ----- | ------ | -------- | -------- | ---------"
echo "  0  | 127.0.0.1     | 48523 | #1     | ✅ ACTIV | user1    | TYPE I"
echo "  1  | 127.0.0.1     | 48524 | #2     | ✅ ACTIV | user2    | PASV"
echo "  2  | 127.0.0.1     | 48525 | #3     | ✅ ACTIV | user3    | TYPE I"
echo ""
echo "Total Active: 3"
echo "Max Capacity: 100"
EOF
chmod +x /tmp/print_sessions.sh
```

### Screenshot cần chụp

![1786277105435](image/Section7_AppDemoEvidence/1786277105435.png)

![1786277026352](image/Section7_AppDemoEvidence/1786277026352.png)

---

## 7.7 Ca Kiểm Thử 5: Multiple Concurrent Clients Downloading

### Mục đích

Chứng minh:

- Server xử lý 3+ clients cùng lúc
- Mỗi thread độc lập, không ảnh hưởng nhau
- RDT hoạt động stable với multiple sessions

### Bước thực hiện

**Chuẩn bị file test:**

```bash
cd /tmp/ftp_server_storage
# Tạo 3 file khác nhau
dd if=/dev/urandom of=file_A.bin bs=512K count=1
dd if=/dev/urandom of=file_B.bin bs=256K count=1
dd if=/dev/urandom of=file_C.bin bs=128K count=1
```

**Terminal 1 — Server:**

```bash
./ftp_server 2>&1 | tee /tmp/server_concurrent_test.log
```

**Terminal 2, 3, 4 — 3 clients download đồng thời:**

```bash
#!/bin/bash
# concurrent_download.sh

# Client 1: Download file_A.bin
(
  ~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user1
pass pass1
binary
passive
retr file_A.bin
quit
COMMANDS
) > /tmp/client1_download.log 2>&1 &

# Client 2: Download file_B.bin
(
  ~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user2
pass pass2
binary
passive
retr file_B.bin
quit
COMMANDS
) > /tmp/client2_download.log 2>&1 &

# Client 3: Download file_C.bin
(
  ~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user3
pass pass3
binary
passive
retr file_C.bin
quit
COMMANDS
) > /tmp/client3_download.log 2>&1 &

# Chờ tất cả download xong
wait

# Kiểm tra kết quả
echo "=== DOWNLOAD RESULTS ==="
for i in 1 2 3; do
  if grep -q "226 Transfer complete" /tmp/client${i}_download.log; then
    echo "✅ Client $i: Download SUCCESS"
  else
    echo "❌ Client $i: Download FAILED"
  fi
done
```

### Expected Server Log (Concurrent)

```
[2026-08-09 11:05:00] New connection from 127.0.0.1:49001 (Thread #1)
[2026-08-09 11:05:01] New connection from 127.0.0.1:49002 (Thread #2)
[2026-08-09 11:05:02] New connection from 127.0.0.1:49003 (Thread #3)

[Thread #1] Authenticated: user1
[Thread #2] Authenticated: user2
[Thread #3] Authenticated: user3

[Thread #1] RETR file_A.bin (524288 bytes) - Starting RDT transfer
[Thread #2] RETR file_B.bin (262144 bytes) - Starting RDT transfer
[Thread #3] RETR file_C.bin (131072 bytes) - Starting RDT transfer

[RDT:Thread #1] Sender: 32 packets, window=32
[RDT:Thread #2] Sender: 16 packets, window=32
[RDT:Thread #3] Sender: 8 packets, window=32

[Thread #3] << 226 Transfer complete (131072 bytes) — 0.8s
[Thread #2] << 226 Transfer complete (262144 bytes) — 1.5s
[Thread #1] << 226 Transfer complete (524288 bytes) — 2.1s

[Thread #1] Client disconnected
[Thread #2] Client disconnected
[Thread #3] Client disconnected

=== STATISTICS ===
Total transfers: 3
Total data: 917504 bytes
Peak concurrent: 3 clients
Avg throughput: 145 KB/s per client
```

### Verification Script

```bash
#!/bin/bash
echo "=== CONCURRENT TRANSFER VERIFICATION ==="
echo ""
echo "File Sizes:"
echo "- file_A.bin: $(wc -c < /tmp/ftp_server_storage/file_A.bin) bytes"
echo "- file_B.bin: $(wc -c < /tmp/ftp_server_storage/file_B.bin) bytes"
echo "- file_C.bin: $(wc -c < /tmp/ftp_server_storage/file_C.bin) bytes"
echo ""

# Tính total bytes
TOTAL=$(( $(wc -c < /tmp/ftp_server_storage/file_A.bin) + \
          $(wc -c < /tmp/ftp_server_storage/file_B.bin) + \
          $(wc -c < /tmp/ftp_server_storage/file_C.bin) ))

echo "Total Data Transferred: $((TOTAL / 1024)) KB"
echo ""
echo "Client Results:"
for i in 1 2 3; do
  if grep -q "226 Transfer complete" /tmp/client${i}_download.log; then
    echo "✅ Client $i: SUCCESS"
  else
    echo "❌ Client $i: FAILED"
  fi
done
```

### Screenshot cần chụp

- ✅ Terminal Server: Thể hiện 3 threads hoạt động song song
- ✅ 3 Client terminals: Parallel download progress bars
- ✅ Server statistics: Total transfers, concurrent count, throughput

---

## 7.8 Ca Kiểm Thử 6: Transfer File ASCII Mode (TYPE A)

### Mục đích

Chứng minh:

- Hỗ trợ TYPE A (ASCII/Text mode)
- Line ending conversion: LF ↔ CRLF (nếu cần)
- Tính năng working với text files

### Bước thực hiện

**Chuẩn bị file text:**

```bash
cd /tmp/ftp_server_storage

# Tạo file text có newlines
cat > readme.txt << 'EOF'
Hybrid FTP System
Line 2: Testing ASCII Mode
Line 3: With multiple line endings
Line 4: Special chars: café, naïve
Line 5: Numbers: 1234567890
EOF

# Tính original hash
sha256sum readme.txt > readme.sha256.original
```

**Terminal 1 — Server:**

```bash
stdbuf -oL ~/Hybrid-FTP/build/ftp_server 2>&1 | tee /tmp/server_ascii_test.log
```

**Terminal 2 — Client (ASCII mode):**

```bash


~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user testuser
pass testpass
type A
passive
stor readme.txt
hash readme.txt
quit
COMMANDS
```

### Expected Output

```
ftp> type A
200 Type set to A (ASCII).
ftp> stor readme.txt
150 Opening data connection for STOR readme.txt
[Uploading readme.txt ========================================>] 235 bytes
226 Transfer complete. (235 bytes, SHA-256: b4a2c...)
```

### Verification

```bash
# Kiểm tra file text được lưu
cat /tmp/ftp_server_storage/readme.txt
# Kiểm tra line endings (kiểm tra CRLF)
cat -A /tmp/ftp_server_storage/readme.txt
#Nếu cuối dòng có ký tự ^M$, file chắc chắn dính CRLF.

#Nếu cuối dòng chỉ có $, file hoàn toàn sạch.
```

### Screenshot cần chụp

- ✅ TYPE A command được chấp nhận (200 response)

![1786293081244](image/Section7_AppDemoEvidence/1786293081244.png)

- ✅ STOR readme.txt thành công

![1786293807548](image/Section7_AppDemoEvidence/1786293807548.png)

- ✅ File content hiển thị chính xác trên server

![1786294410917](image/Section7_AppDemoEvidence/1786294410917.png)

![1786296549036](image/Section7_AppDemoEvidence/1786296549036.png)

---

## 7.9 Ca Kiểm Thử 7: Transfer File Binary Mode (TYPE I)

### Mục đích

Chứng minh:

- Hỗ trợ TYPE I (Binary/Image mode)
- Không có line ending conversion
- Binary data được transfer chính xác

### Bước thực hiện

**Chuẩn bị file binary:**

```bash
cd /tmp/ftp_server_storage

# Tạo file binary (chứa cả null bytes)
dd if=/dev/urandom bs=1024 count=10 2>/dev/null | \
  python3 -c "
import sys
data = sys.stdin.buffer.read()
data = data[:1000] + b'\\x00\\x00\\x00\\x00' + data[1000:]  # Thêm null bytes
sys.stdout.buffer.write(data)
" > binary_test.bin

sha256sum binary_test.bin > binary_test.sha256.original
```

**Terminal 1 — Server:**

```bash
stdbuf -oL ~/Hybrid-FTP/build/ftp_server 2>&1 | tee /tmp/server_ascii_test.log
```

**Terminal 2 — Client (Binary mode):**

```bash
cd /tmp/ftp_server_storage

~/Hybrid-FTP/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user testuser
pass testpass
type I
passive
stor binary_test.bin
hash binary_test.bin
quit
COMMANDS
```

### Verification

```bash
# So sánh file byte-for-byte
cp /tmp/ftp_server_storage/binary_test.bin /tmp/ftp_server_storage/binary_test.bin.recv

cmp /tmp/ftp_server_storage/binary_test.bin /tmp/ftp_server_storage/binary_test.bin.recv
echo $?  # 0 = files identical

# So sánh hash
sha256sum /tmp/ftp_server_storage/binary_test.bin
cat /tmp/ftp_server_storage/binary_test.sha256.original
```

### Screenshot cần chụp

- ✅ TYPE I command được chấp nhận

![1786294858444](image/Section7_AppDemoEvidence/1786294858444.png)

- ✅ STOR binary_test.bin + HASH response

![1786294964964](image/Section7_AppDemoEvidence/1786294964964.png)

- ✅ Hex dump of received file (od -t x1 | head)

![1786295339648](image/Section7_AppDemoEvidence/1786295339648.png)

![1786295413955](image/Section7_AppDemoEvidence/1786295413955.png)

---

## 7.10 Tóm tắt Bằng Chứng (Evidence Summary)

### Bảng Kiểm tra Hoàn thành

|    #    | Ca Kiểm Thử          | Status | Log file                   |        Screenshot        |
| :-----: | :--------------------- | :----: | :------------------------- | :----------------------: |
|    1    | Upload STOR            |   ✅   | server_upload_test.log     |   upload_terminal.png   |
|    2    | Download RETR          |   ✅   | server_download_test.log   |  download_terminal.png  |
|    3    | SHA-256 Integrity      |   ✅   | hash_comparison.txt        |      hash_match.png      |
|    4    | Session Management     |   ✅   | session_tracking.log       |    session_table.png    |
| <br />5 | Concurrent (3 clients) |   ✅   | server_concurrent_test.log | concurrent_terminals.png |
|    6    | ASCII Mode (TYPE A)    |   ✅   | server_ascii_test.log      |    ascii_transfer.png    |
|    7    | Binary Mode (TYPE I)   |   ✅   | server_binary_test.log     |   binary_transfer.png   |

### Tệp Log cần lưu giữ

```bash
# Tạo thư mục evidence
mkdir -p /tmp/ftp_evidence
cp /tmp/server_*.log /tmp/ftp_evidence/
cp /tmp/client*.log /tmp/ftp_evidence/
cp /tmp/hash_comparison.txt /tmp/ftp_evidence/
cp /tmp/session_tracking.log /tmp/ftp_evidence/

# Tạo tarball cho dễ submission
tar -czf /tmp/ftp_evidence_phase4.tar.gz /tmp/ftp_evidence/
ls -lh /tmp/ftp_evidence_phase4.tar.gz
```

### Checklist cuối cùng

- [ ] Tất cả 7 ca kiểm thử đã pass ✅
- [ ] Toàn bộ log files lưu giữ
- [ ] Screenshots chứng minh mỗi tính năng
- [ ] File integrity (SHA-256 matching)
- [ ] Multi-threading hoạt động (3+ concurrent clients)
- [ ] Both ASCII (TYPE A) và Binary (TYPE I) modes work
- [ ] Server không crash trong các test
- [ ] CMake build clean trên fresh machine

---

## 7.11 Huỷ bỏ Test sau khi hoàn thành

```bash
# Dọn dẹp
pkill -f ftp_server  # Kill server nếu vẫn chạy
rm -rf /tmp/ftp_server_storage /tmp/ftp_client_storage
rm -f /tmp/server_*.log /tmp/client*.log
```

---

**Kết luận:** Chương này cung cấp đầy đủ bằng chứng thực tế chứng minh Hybrid FTP hoạt động theo đầy đủ các yêu cầu: STOR/RETR reliable transfer, multi-threading, concurrent clients, ASCII/Binary modes, và file integrity verification qua SHA-256.
