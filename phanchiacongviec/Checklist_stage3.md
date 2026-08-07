# HYBRID FTP — DỰ ÁN LẬP TRÌNH SOCKET

## Checklist Giai đoạn 3: Tích hợp hệ thống & Kiểm thử (Ngày 8–10)

## 0. Việc tồn đọng từ Giai đoạn 2 (bắt buộc xong trước khi tích hợp)

Rà lại code hiện tại cho thấy các phần này của Giai đoạn 2 chưa hoàn thành hoặc chưa nối vào hệ thống — phải xử lý trước Ngày 8, nếu không RETR/STOR sẽ không có gì để gọi tới.

### Dev 1 — TCP Control

- Cài đặt lệnh **TYPE {A|I}** — set chế độ ASCII/Binary vào SessionState, ảnh hưởng cách FileHandler đọc/ghi ở Ngày 8
- Cài đặt lệnh **MODE {S|B|C}** — tối thiểu chấp nhận Stream mode, trả 200 OK
- Sửa mã phản hồi LIST/NLST/STAT đang dùng **212/213** (sai chuẩn RFC 959) → đổi về đúng nhóm 1xx/2xx theo bảng mã trong đề bài
- **File:** `src/server/Session.cpp`, `src/server/Session.h` (thêm field `transferType`, `transferMode` vào SessionState)

### Dev 2 — RDT/UDP

- Thêm vòng lặp **timeout + retransmit** vào `RDTSender::sendPacket` — hiện tại chỉ gửi 1 lần rồi trả false nếu không có ACK, cần retry tối đa N lần trước khi báo lỗi hẳn
- **File:** `src/rdt/ReliableTransfer.cpp`, `src/rdt/ReliableTransfer.h` (thêm `maxRetries`, `retryCount`)

### Dev 3 — Active/Passive & HASH

- Cài đặt lệnh **PORT \<h1,h2,h3,h4,p1,p2\>** — parse IP/port client gửi, lưu vào SessionState để dùng khi mở data channel
- Cài đặt lệnh **PASV** — server mở UDPSocket ở cổng ngẫu nhiên (bind(0), getLocalPort()), trả về IP+port dạng (h1,h2,h3,h4,p1,p2)
- Nối lệnh **HASH \<filename\>** vào Session.cpp — gọi `CryptoHash::computeSHA256FromFile()`, hàm tính hash đã có sẵn nhưng chưa được gọi ở đâu cả
- **File:** `src/server/Session.cpp`, `src/server/Session.h` (thêm `dataChannelIP`, `dataChannelPort`, `isPassiveMode`)

## 1. Bảng phân vai tổng quan

| Role | Phụ trách chính | File chính sẽ đụng tới |
| --- | --- | --- |
| Dev 1 | TCP Control Channel: lệnh TYPE/MODE/PORT/PASV, mã phản hồi chuẩn, điều phối RETR/STOR phía control | `src/server/Session.cpp`, `src/server/Session.h`, `src/common/CommandParser.cpp` |
| Dev 2 | RDT/UDP: timeout-retransmit, Sliding Window, tích hợp RDT vào luồng transfer thật, ABOR | `src/rdt/ReliableTransfer.cpp/.h`, `src/rdt/UDPSocket.cpp/.h`, `src/rdt/CustomUDPHeader.h` |
| Dev 3 | File I/O thật cho RETR/STOR, HASH command, STOU/APPE, đa client, demo evidence | `src/common/FileHandler.cpp`, `src/common/CryptoHash.cpp`, `src/server/Session.cpp` (nhánh HASH/STOU/APPE), `src/server/ServerManager.cpp` |

## Ngày 8 — Ghép nối luồng Upload / Download

### Dev 1 — Điều phối lệnh RETR / STOR trên Control Channel

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Implement RETR \<filename\>: kiểm tra file tồn tại (PathManager::getFileSize), gửi 150 Opening data connection, chờ Dev 2/Dev 3 truyền xong, gửi 226 Transfer complete | `src/server/Session.cpp` | Thêm nhánh `command.name == "RETR"`: validate quyền/đường dẫn, gọi hàm trung gian `sendFileOverDataChannel()` (Dev 3 viết), trả reply đúng theo kết quả trả về |
| Implement STOR \<filename\>: gửi 150, chờ nhận file qua data channel, ghi xong gửi 226; nếu lỗi mạng gửi 426 | `src/server/Session.cpp` | Nhánh `command.name == "STOR"`: gọi `receiveFileOverDataChannel()` (Dev 3), xử lý case ghi đè file đã tồn tại |
| Đảm bảo control channel không bị block khi data channel đang truyền file lớn (nếu single-thread per session thì OK vì mỗi client đã có thread riêng) | `src/server/Session.cpp` | Kiểm tra lại `handle_client_thread` — xác nhận luồng RETR/STOR chạy tuần tự trong đúng thread của client đó, không đụng thread khác |
| Cập nhật HELP text đã có sẵn RETR/STOR — kiểm tra lại field syntaxTable khớp với mô tả thực tế đã implement | `src/server/Session.cpp` | Đối chiếu bảng `syntaxTable` trong `handle_help_command()` với hành vi thực tế |

### Dev 2 — RDT vận hành thật trên data channel

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Bổ sung API chia file thành nhiều gói (chunk theo MTU ~1024–1400 byte) và gửi tuần tự qua RDTSender | `src/rdt/ReliableTransfer.cpp`, `ReliableTransfer.h` | Thêm hàm `sendFile(const std::vector<uint8_t>& data)` lặp gọi `sendPacket()` theo từng chunk, trả false nếu 1 chunk fail sau khi hết retry |
| Bổ sung API nhận nhiều gói liên tiếp cho tới khi có FLAG_FIN, ráp lại thành file hoàn chỉnh | `src/rdt/ReliableTransfer.cpp`, `ReliableTransfer.h` | Thêm hàm `receiveFile(std::vector<uint8_t>& outData)` lặp gọi `receivePacket()` cho tới khi `hasFlag(hdr, FLAG_FIN)` |
| Thêm FLAG_FIN vào gói cuối cùng để receiver biết dừng nhận | `src/rdt/CustomUDPHeader.h` (đã có sẵn FLAG_FIN, chỉ cần dùng) | Sender set FLAG_FIN trên gói cuối; Receiver kiểm tra `hasFlag()` sau khi ráp payload |
| Test round-trip file thật (không chỉ chuỗi text demo) qua RdtClientTest / RdtServerTest | `src/rdt/RdtClientTest.cpp`, `RdtServerTest.cpp` | Sửa demo test để đọc/ghi từ FileHandler thay vì chuỗi cứng, xác nhận file nhận được giống hệt file gửi (so sánh byte-by-byte) |

### Dev 3 — Mở data channel, đọc/ghi file thật, kiểm tra HASH

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Viết hàm mở UDPSocket cho data channel dựa trên mode Active (PORT) hoặc Passive (PASV) đã lưu trong SessionState | `src/server/Session.cpp` hoặc file mới `src/server/DataChannel.cpp` | Active: server sendTo tới IP/port client gửi qua PORT. Passive: server đã bind sẵn từ lệnh PASV, chờ client connect tới |
| Viết `sendFileOverDataChannel(filename, session)`: đọc file bằng FileHandler (theo TYPE A/I), gọi `RDTSender::sendFile()` của Dev 2 | `src/server/Session.cpp`, `src/common/FileHandler.cpp` (đã có readBinaryFile/readTextFile) | Chọn readBinaryFile hay readTextFile dựa trên `session.transferType` |
| Viết `receiveFileOverDataChannel(filename, session)`: gọi `RDTReceiver::receiveFile()` của Dev 2, ghi ra đĩa bằng `FileHandler::writeBinaryFile/writeTextFile` | `src/server/Session.cpp`, `src/common/FileHandler.cpp` | Ghi vào đúng path đã resolve qua `PathManager::resolvePath()`, kiểm tra isInsideRoot trước khi ghi |
| Sau khi RETR/STOR xong, tự động tính hash và log ra console để đối chiếu (chuẩn bị cho HASH command và screenshot minh chứng) | `src/server/Session.cpp`, `src/common/CryptoHash.cpp` (đã có computeSHA256FromFile) | In ra: `"[Session] SHA-256 sau khi transfer: <hash>"` — dùng để so sánh 2 đầu client/server khi demo |
| Test luồng end-to-end đầy đủ: USER→PASS→TYPE→PASV→STOR→(server nhận & ghi file)→HASH→RETR ngược lại→so sánh 2 file | `src/common/test_dev3.sh` | Mở rộng script bash hiện có để test toàn bộ chuỗi lệnh thật, không chỉ PORT/PASV suông như hiện tại |

## Ngày 9 — Tối ưu & Nâng cao (Excellent Level)

### Dev 2 — Sliding Window / Congestion Control

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Mở rộng RDTSender từ Stop-and-Wait sang Sliding Window: gửi nhiều gói trong 1 cửa sổ trước khi chờ ACK | `src/rdt/ReliableTransfer.cpp`, `ReliableTransfer.h` | Thêm `windowSize`, buffer các gói chưa được ACK, base/nextSeqNum theo mô hình Go-Back-N hoặc Selective Repeat |
| Xử lý timeout theo từng gói (hoặc theo window) khi dùng Sliding Window | `src/rdt/ReliableTransfer.cpp` | Go-Back-N: 1 timer cho cả window, hết hạn thì gửi lại toàn bộ từ base. Selective Repeat: timer riêng từng gói |
| Cập nhật `CustomUDPHeader.windowSize` để phản ánh cửa sổ hiện tại (đã có field sẵn trong struct, mới set = 1) | `src/rdt/CustomUDPHeader.h`, `CustomHeader.cpp` | Set windowSize theo giá trị cấu hình thay vì hard-code 1 |

### Dev 1 & Dev 3 — Edge cases

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Implement ABOR: nhận lệnh giữa lúc đang transfer, đóng data channel, trả 426 Connection closed | `src/server/Session.cpp` | Cần cơ chế cờ hiệu (flag) để luồng transfer đang chạy kiểm tra và dừng giữa chừng — cân nhắc `atomic<bool> abortRequested` trong SessionState |
| Implement STOU: tạo tên file duy nhất (vd: thêm timestamp hoặc số đếm) khi upload để tránh ghi đè | `src/server/Session.cpp`, `src/common/PathManager.cpp` | Thêm hàm `PathManager::generateUniqueFilename(baseName)` kiểm tra fileExists lặp tới khi ra tên chưa dùng |
| Implement APPE: ghi nối vào file đã tồn tại thay vì ghi đè, tạo mới nếu file chưa có | `src/server/Session.cpp`, `src/common/FileHandler.cpp` | Thêm tham số `append` vào `writeBinaryFile/writeTextFile` (dùng `std::ios::app`) hoặc hàm mới `appendBinaryFile` |
| Xử lý client ngắt kết nối đột ngột giữa transfer (TCP recv trả 0/lỗi trong lúc RETR/STOR đang chạy) | `src/server/Session.cpp`, `src/rdt/ReliableTransfer.cpp` | Đảm bảo không leak thread/socket, dọn dẹp data channel UDP khi phát hiện control channel đứt |

## Ngày 10 — Bug fixing & Chuẩn bị Demo

| Việc cần làm | File | Chi tiết kỹ thuật |
| --- | --- | --- |
| Code Freeze — không thêm tính năng mới, chỉ sửa lỗi | Toàn bộ repo | Cả nhóm thống nhất giờ freeze, commit cuối cùng đánh dấu rõ trong Git log |
| Build sạch trên máy mới (clean machine / xóa thư mục build) | `CMakeLists.txt` | `rm -rf build && cmake -B build && cmake --build build` — xác nhận cả ftp_server và ftp_client compile không lỗi/không warning nghiêm trọng |
| Test nhiều client đồng thời, xác nhận session table đúng | `src/server/ServerManager.cpp` (clients_print đã có sẵn) | Mở 3-4 terminal chạy ftp_client cùng lúc, gọi `clients_print()` để in bảng, kiểm tra không bị race condition (đã có pthread_mutex_lock) |
| Test cả file ASCII (.txt) và Binary (ảnh/video nhỏ) qua RETR/STOR | `src/common/test_dev3.sh` | Dùng lại `test_binary.bin` và `test_sample.txt` đã có sẵn trong script, mở rộng thêm bước STOR/RETR thật |
| Chụp ảnh màn hình: upload thành công, download thành công, HASH khớp giữa 2 đầu, bảng session nhiều client | `docs/` (thư mục ảnh demo) | Lưu tất cả screenshot vào `docs/demo_evidence/` để dùng cho Technical_Report.docx ở Giai đoạn 4 |
| Kiểm tra log server hiển thị đúng: IP client, lệnh đã thực thi, trạng thái session | `src/server/Session.cpp`, `ServerManager.cpp` (printf logs đã có sẵn) | Rà lại toàn bộ các dòng `printf("[Session]...")` và `printf("[Server]...")` đã đủ thông tin để demo trước giám khảo chưa |

## Ghi chú tổng hợp file theo người

**Dev 1 chủ yếu làm việc trong:**
`src/server/Session.cpp` · `src/server/Session.h` · `src/common/CommandParser.cpp`

**Dev 2 chủ yếu làm việc trong:**
`src/rdt/ReliableTransfer.cpp` · `src/rdt/ReliableTransfer.h` · `src/rdt/UDPSocket.cpp` · `src/rdt/CustomUDPHeader.h`

**Dev 3 chủ yếu làm việc trong:**
`src/common/FileHandler.cpp` · `src/common/CryptoHash.cpp` · `src/common/PathManager.cpp` · `src/server/Session.cpp` (phần data channel) · `src/common/test_dev3.sh`

**File dùng chung, cần thống nhất trước khi sửa song song:**
`src/server/Session.h` (SessionState) — nên thêm field 1 lần rồi báo nhóm, tránh conflict Git; `src/server/ServerManager.h/.cpp` — chỉ Dev 3/Dev 1 sửa khi cần, tránh đụng logic thread của nhau.
