# Section 2.2 — UDP Custom Header Fields

> **Phụ trách:** Dev 2 — Reliable UDP & Thuật toán RDT

---

## Tổng quan

Dự án Hybrid FTP sử dụng một **kênh dữ liệu UDP tùy chỉnh** thay thế cho luồng TCP thông thường nhằm tối đa hóa thông lượng truyền file lớn. Để đảm bảo tính tin cậy trên nền UDP vốn không đảm bảo thứ tự và không xác nhận, nhóm tự thiết kế một header 15 byte bổ sung vào phía trước mỗi datagram UDP — gọi là **`CustomUDPHeader`**.

Header được khai báo với `#pragma pack(push, 1)` (trong `CustomUDPHeader.h`) để đảm bảo **không có byte padding**, đúng với kích thước vật lý khi truyền qua mạng.

---

## Bố cục byte của CustomUDPHeader

Tổng kích thước: **15 byte** (= `CUSTOM_UDP_HEADER_SIZE`)

```
 Byte Offset  Kích thước   Tên trường      Kiểu C++
─────────────┼──────────────┼────────────────┼─────────────
     0 - 3       4 byte      seqNum           uint32_t
     4 - 7       4 byte      ackNum           uint32_t
     8 - 9       2 byte      payloadLen       uint16_t
    10 - 11      2 byte      windowSize       uint16_t
    12 - 13      2 byte      checksum         uint16_t
       14         1 byte      flags            uint8_t
```

> **Byte order:** Tất cả trường đa byte được chuyển sang **Network Byte Order (Big-Endian)** khi serialize (hàm `serializeHeader`) và chuyển ngược lại khi deserialize (hàm `deserializeHeader`). Cụ thể sử dụng `htonl` / `htons` / `ntohl` / `ntohs` từ `<arpa/inet.h>` (Linux).

---

## Mô tả chi tiết từng trường

### 1. `seqNum` — Sequence Number (4 byte, uint32_t)

- **Ý nghĩa:** Số thứ tự của **segment** trong luồng truyền hiện tại.
- **Đơn vị:** Mỗi đơn vị tương ứng với **một segment** (không tính theo byte như TCP). Segment đầu tiên có `seqNum = 0`, tiếp theo là 1, 2, ...
- **Dùng bởi:** Sender gán `seqNum` tăng dần cho mỗi gói DATA. Receiver kiểm tra `seqNum` so với `expectedSeq_` để phát hiện gói lệch thứ tự (Go-Back-N).
- **Trường hợp FIN:** Gói kết thúc luồng (FLAG_FIN) gán `seqNum = N` (tổng số segment đã gửi).

---

### 2. `ackNum` — Acknowledgment Number (4 byte, uint32_t)

- **Ý nghĩa:** Số thứ tự của **segment cuối cùng đã nhận thành công** (cumulative ACK). Receiver xác nhận rằng đã nhận đúng thứ tự tất cả segment từ 0 đến `ackNum`.
- **Quan hệ với `expectedSeq_`:** `ackNum = expectedSeq_ - 1`.
- **Trong gói DATA của Sender:** Trường này được đặt `= 0` (không có nghĩa).
- **Trong gói ACK của Receiver:** Chứa `ackNum` của segment cuối đã nhận.
- **Trong FINACK:** `ackNum = seqNum` của gói FIN nhận được.

---

### 3. `payloadLen` — Payload Length (2 byte, uint16_t)

- **Ý nghĩa:** Độ dài tính bằng byte của phần dữ liệu (payload) đi kèm sau header trong cùng một datagram UDP.
- **Giá trị tối đa:** 65535 byte (giới hạn kiểu `uint16_t`). Trong thực tế giới hạn bởi MSS = 1024 byte (mặc định).
- **Trong gói ACK / FIN / FINACK:** `payloadLen = 0` vì không mang dữ liệu.
- **Mục đích:** Cho phép receiver tách chính xác header khỏi payload khi đọc từ socket buffer.

---

### 4. `windowSize` — Window Size (2 byte, uint16_t)

- **Ý nghĩa:** Số segment tối đa mà bên kia **sẵn sàng nhận** (hoặc có thể gửi) tại thời điểm này.
- **Flow Control (Receiver → Sender):** Trong mỗi gói ACK, Receiver điền `windowSize = advertisedWindow_` để thông báo buffer còn trống. Sender đọc giá trị này và cập nhật `recvWindow_`, giới hạn số gói in-flight.
- **Congestion Control (Sender → Receiver):** Trong mỗi gói DATA, Sender điền `windowSize = computeEffectiveWindow()` (= `min(cwnd, recvWindow_, maxWindowSegments_)`).
- **Deadlock prevention:** Nếu `recvWindow_ == 0`, sender vẫn gửi tối thiểu 1 segment để duy trì kết nối (tránh deadlock giống TCP zero-window probe).

---

### 5. `checksum` — Checksum (2 byte, uint16_t)

- **Ý nghĩa:** Giá trị kiểm tra toàn vẹn dữ liệu của gói, tính trên **toàn bộ [header + payload]**.
- **Thuật toán:** **Internet Checksum** (tương tự RFC 793 — TCP checksum), tính bằng hàm `internetChecksum()`:
  1. Đặt `checksum = 0` trong bản sao header.
  2. Serialize header và nối với payload thành một buffer liên tục.
  3. Chia buffer thành các **word 16-bit**, cộng dồn tất cả.
  4. Xử lý carry (cộng carry vào tổng nếu tổng tràn 16-bit).
  5. Lấy bù **1** (bitwise NOT): `~sum & 0xFFFF`.
- **Xác thực:** Hàm `verifyChecksum()` tính lại checksum và so sánh với giá trị trong header. Nếu không khớp → gói bị hỏng → bỏ qua và gửi lại ACK cũ.

---

### 6. `flags` — Control Flags (1 byte, uint8_t)

Byte cuối của header, mỗi bit đại diện cho một cờ điều khiển độc lập:

```
  Bit 7  Bit 6  Bit 5  Bit 4  Bit 3  Bit 2  Bit 1  Bit 0
+------+------+------+------+------+------+------+------+
|  (0) |  (0) |  (0) |  NAK |  FIN | DATA |  ACK |  SYN |
+------+------+------+------+------+------+------+------+
```

| Flag        | Bit | Giá trị hex | Ý nghĩa                                                        |
|-------------|-----|-------------|----------------------------------------------------------------|
| `FLAG_SYN`  | 0   | `0x01`      | Synchronize — Thiết lập kết nối (dự phòng, hiện không dùng)  |
| `FLAG_ACK`  | 1   | `0x02`      | Acknowledgment — Gói này là xác nhận nhận dữ liệu            |
| `FLAG_DATA` | 2   | `0x04`      | Data — Gói này mang payload dữ liệu file                      |
| `FLAG_FIN`  | 3   | `0x08`      | Finish — Kết thúc luồng truyền (tương tự FIN của TCP)         |
| `FLAG_NAK`  | 4   | `0x10`      | Negative Acknowledgment — Từ chối rõ ràng (dự phòng)          |

**Các tổ hợp flags thực tế trong giao thức:**

| Gói tin      | Flags đặt              | Mô tả                                   |
|--------------|------------------------|-----------------------------------------|
| DATA segment | `FLAG_DATA`            | Gói dữ liệu thông thường               |
| ACK          | `FLAG_ACK`             | Xác nhận cumulative                     |
| FIN          | `FLAG_FIN`             | Sender báo hiệu hết dữ liệu             |
| FINACK       | `FLAG_FIN + FLAG_ACK`  | Receiver xác nhận đã nhận FIN          |

**Thao tác với flags** — nhóm sử dụng 3 hàm inline an toàn:
```cpp
setFlag(hdr, FLAG_DATA);          // Bật bit cờ
clearFlag(hdr, FLAG_ACK);         // Tắt bit cờ
bool ok = hasFlag(hdr, FLAG_FIN); // Kiểm tra bit cờ
```

---

## Sơ đồ cấu trúc gói tin đầy đủ (Wire Format)

```
+------------------------------------------------------------------------------+
|                      UDP Datagram (kênh dữ liệu)                            |
+------------------------------------------------------------------------------+
|     UDP Header chuẩn (8 byte) — xử lý bởi OS/Kernel                        |
|  [Src Port 2B] [Dst Port 2B] [Length 2B] [Checksum 2B]                      |
+-----------------------------------------------+--------------+--------------+
|          Custom UDP Header (15 byte)           |              |              |
|  [seqNum 4B] [ackNum 4B] [payloadLen 2B]       |   Payload    |              |
|  [windowSize 2B] [checksum 2B] [flags 1B]      | (0-1024 byte)|              |
+-----------------------------------------------+--------------+--------------+
```

---

## So sánh: UDP thuần vs UDP + Custom Header

| Tiêu chí              | UDP thuần              | UDP + CustomUDPHeader                 |
|-----------------------|------------------------|---------------------------------------|
| Đảm bảo thứ tự        | Không                  | Có (qua seqNum + expectedSeq_)        |
| Phát hiện lỗi         | Checksum 16-bit cơ bản | Internet Checksum toàn bộ header+data |
| Kiểm soát luồng       | Không                  | Có (qua windowSize trong ACK)         |
| Kiểm soát tắc nghẽn   | Không                  | Có (AIMD, Slow Start trong Sender)    |
| Kết thúc luồng rõ ràng| Không                  | Có (FLAG_FIN + FINACK handshake)      |
