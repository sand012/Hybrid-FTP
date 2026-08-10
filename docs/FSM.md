# Section 3.2 — RDT Sender/Receiver State Machines

---

## Tổng quan kiến trúc

Module RDT của dự án Hybrid FTP được tổ chức thành 2 lớp:

- **`RDTWindowSender`** (trong `SlidingWindow.h/cpp`): Triển khai Go-Back-N Sliding Window với Slow Start + AIMD (TCP Reno rút gọn).
- **`RDTWindowReceiver`** (trong `SlidingWindow.h/cpp`): Triển khai GBN Receiver với cumulative ACK và quảng bá Flow Control Window.
- **`RDTSender` / `RDTReceiver`** (trong `ReliableTransfer.h/cpp`): Lớp bọc mỏng (wrapper) cung cấp API cấp cao hơn (`sendFile`, `sendBuffer`, `receiveFile`, `receiveBuffer`) và backward-compatible Stop-and-Wait (`sendPacket`/`receivePacket`).

---

## FSM 1 — RDTWindowSender (Go-Back-N + Slow Start/AIMD)

### Các biến trạng thái chính

| Biến              | Ý nghĩa                                                             |
|-------------------|---------------------------------------------------------------------|
| `base`            | Seq nhỏ nhất chưa được ACK (cạnh trái cửa sổ)                       |
| `nextSeq`         | Seq tiếp theo sẽ gửi (cạnh phải "gửi được")                         |
| `cwnd_`           | Congestion Window (số segment) — thay đổi theo Slow Start / AIMD    |
| `ssthresh_`       | Slow Start Threshold — ngưỡng chuyển từ SS sang CA                  |
| `recvWindow_`     | Receive Window quảng bá bởi receiver (flow control)                 |
| `retryRnd`        | Số vòng timeout liên tiếp (đặt lại khi nhận ACK hợp lệ)             |

### Công thức tính Effective Window

```basch
effectiveWindow = min(floor(cwnd_), recvWindow_, maxWindowSegments_)
                  max(effectiveWindow, 1)   // không bao giờ = 0
```

---

### Lưu đồ máy trạng thái — RDTWindowSender

```map
                         ┌─────────────────────┐
                         │        START         │
                         │  Chia data → segs[]  │
                         │  cwnd=1, ssthresh=8  │
                         │  base=0, nextSeq=0   │
                         └──────────┬───────────┘
                                    │
                                    ▼
                    ┌─────────────────────────────── ┐
                    │         [Kiểm tra ABOR]        │
                    │  shouldCancel_() == true?      │
                    └──────┬────────────────┬────────┘
                    Có     │                │  Không
                           ▼                │
                      RETURN false          │
                                            ▼
                    ┌───────────────────────────────────┐
                    │   PHÁT SÓNG (Fill Window)         │
                    │  while nextSeq < base + wnd:      │
                    │    sendSegment(segs[nextSeq])     │
                    │    nextSeq++                      │
                    └───────────────┬───────────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────────┐
                    │         CHỜ ACK (recvFrom)        │
                    │         với timeout = 500ms       │
                    └──────┬──────────────────┬─────────┘
                           │                  │
                    TIMEOUT│                  │Nhận được gói
                           ▼                  ▼
          ┌───────────────────── ┐   ┌──────────────────────────── ┐
          │   TIMEOUT HANDLING   │   │   Kiểm tra gói nhận         │
          │  totalTimeouts_++    │   │   verifyChecksum()?         │
          │  retryRnd++          │   └───┬────────────────┬────────┘
          │                      │  Sai  │                │ Đúng
          │  retryRnd > maxRnd?  │       ▼                │
          │  ┌──Yes──► RETURN    │  Bỏ qua,          ┌────▼─────────────────┐
          │  │         false     │  tiếp tục chờ     │  hasFlag(FLAG_ACK)?  │
          │  │                   │                   └──┬───────────────┬───┘
          │  No                  │               Không  │               │ Có
          │  ssthresh=cwnd/2     │                   ▼  │               ▼
          │  cwnd = 1            │         Bỏ qua,   │  │   Đọc ackNum, rxWindow
          │  nextSeq = base      │         tiếp tục  │  │   recvWindow_ = rxWindow
          │  (Go-Back-N retrans) │                   │  │
          └─────────────────────┘                   │  ▼
                    │                               │  ┌────────────────────────┐
                    │                               │  │  ackNum >= base?       │
                    │                               │  └──┬────────────────┬────┘
                    │                               │  Không               │ Có
                    │                               │     │                │
                    │                               │     ▼                ▼
                    │                               │  Bỏ qua    ┌───────────────────────  ──┐
                    │                               │            │  SLIDE WINDOW             │
                    │                               │            │  newAcked = ackNum+1-base │
                    │                               │            │  Lặp newAcked lần:        │
                    │                               │            │    if cwnd < ssthresh:    │
                    │                               │            │      cwnd += 1.0  (SS)    │
                    │                               │            │    else:                  │
                    │                               │            │      cwnd += 1/cwnd (CA)  │
                    │                               │            │  cwnd = min(cwnd, maxWin) │
                    │                               │            │  base = ackNum + 1        │
                    │                               │            │  retryRnd = 0             │
                    │                               │            └────────────┬────────────  ┘
                    │                               │                         │
                    └───────────────────────────────┴─────────────────────────┘
                                                    │
                                    ┌───────────────┴─────────────────┐
                                    │       base >= N (tất cả ACK)?   │
                                    └──┬────────────────────┬─────────┘
                                  Không│                    │ Có
                                       │                    ▼
                                       │       ┌─────────────────────────────┐
                                       │       │  GỬI FIN, CHỜ FINACK        │
                                       │       │  Lặp tối đa maxRnd lần:     │
                                       │       │    sendFIN()                │
                                       │       │    recvFrom(timeout)        │
                                       │       │    if FLAG_FIN + FLAG_ACK:  │
                                       │       │      RETURN true            │
                                       │       │  RETURN false (hết retry)   │
                                       │       └─────────────────────────────┘
                                       │
                              Quay lại PHÁT SÓNG
```

---

### Các giai đoạn Congestion Control

#### Giai đoạn 1 — Slow Start (cwnd < ssthresh)

```table
Mỗi ACK hợp lệ nhận được:
    cwnd = cwnd + 1.0
```

- cwnd tăng **theo cấp số nhân**: sau mỗi RTT cwnd tăng gấp đôi (vì mỗi gói trong cửa sổ đều được ACK).
- Dừng Slow Start khi `cwnd >= ssthresh`.

#### Giai đoạn 2 — Congestion Avoidance (cwnd >= ssthresh)

```table
Mỗi ACK hợp lệ nhận được:
    cwnd = cwnd + (1.0 / cwnd)
```

- cwnd tăng **tuyến tính**: xấp xỉ +1 segment mỗi RTT.
- Đây là giai đoạn "Additive Increase" trong AIMD.

#### Sự kiện Timeout — Multiplicative Decrease

```table
Khi timeout:
    ssthresh = max(cwnd / 2.0, 1.0)
    cwnd = 1.0          // Reset về Slow Start
    nextSeq = base      // Go-Back-N: retransmit từ base
    retryRnd++
```

#### Giới hạn cwnd

```table
cwnd = min(cwnd, maxWindowSegments_)   // mặc định = 32 segment
```

---

### Ví dụ tiến trình cwnd qua các sự kiện

```table
RTT  │  cwnd  │  ssthresh  │  Sự kiện
──────┼────────┼────────────┼────────────────────────────────
  0   │   1.0  │    8.0     │  Bắt đầu Slow Start
  1   │   2.0  │    8.0     │  Slow Start: +1 mỗi ACK (2 ACK)
  2   │   4.0  │    8.0     │  Slow Start: +1 mỗi ACK (4 ACK)
  3   │   8.0  │    8.0     │  Slow Start: chạm ssthresh
  4   │   8.5  │    8.0     │  Congestion Avoidance (+1/8 * 4)
  5   │   9.0  │    8.0     │  Congestion Avoidance tiếp tục
  6   │  TIMEOUT│   4.5     │  ssthresh=9/2=4.5, cwnd=1, Go-Back-N
  7   │   1.0  │    4.5     │  Slow Start lại từ đầu
  8   │   2.0  │    4.5     │  Slow Start: +1 mỗi ACK
  9   │   4.0  │    4.5     │  Slow Start: +1 mỗi ACK
 10   │   4.5  │    4.5     │  Chạm ssthresh → Congestion Avoidance
```

---

## FSM 2 — RDTWindowReceiver (GBN Cumulative ACK)

### Các biến trạng thái chính


| Biến               | Ý nghĩa                                                   |
|--------------------|-----------------------------------------------------------|
| `expectedSeq_`     | Seq tiếp theo đang mong đợi (= số segment đã nhận tốt)    |
| `advertisedWindow_`| Kích thước buffer quảng bá cho sender (flow control)      |

---

### Lưu đồ máy trạng thái — RDTWindowReceiver

```map
               ┌─────────────────┐
               │      START       │
               │  expectedSeq_=0  │
               │  outData.clear() │
               └────────┬────────┘
                        │
                        ▼
          ┌─────────────────────────────┐
          │     [Kiểm tra ABOR]          │
          │  shouldCancel_() == true?    │
          └───┬──────────────────┬───────┘
          Có  │                  │  Không
              ▼                  │
        RETURN false             │
                                 ▼
          ┌─────────────────────────────┐
          │   recvFrom(recvBuf, ...)     │
          │   (Chờ gói từ Sender)        │
          └───┬─────────────────────────┘
              │
    ┌─────────▼──────────────────────────────┐
    │  received < CUSTOM_UDP_HEADER_SIZE?     │
    └──┬──────────────────────┬──────────────┘
    Có │                      │ Không
       ▼                      ▼
    TIMEOUT: Nếu        Deserialize header
    expectedSeq_>0,     verifyChecksum?
    gửi lại ACK cũ    ──┬──────────────┬──
    Quay lại chờ     Sai│              │ Đúng
                        ▼              │
                   Gửi lại ACK cũ     │
                   Quay lại chờ       ▼
                               ┌─────────────────┐
                               │ hasFlag(FIN)?   │
                               └──┬──────────┬───┘
                               Có │          │ Không
                                  ▼          ▼
                        ┌────────────────┐  hasFlag(DATA)?
                        │ Gửi FINACK     │  ┌──────────────┐
                        │ (FIN+ACK)      │  │ Có           │ Không
                        │ RETURN true    │  │              ▼
                        └────────────────┘  │       Bỏ qua gói
                                            │       Quay lại chờ
                                            ▼
                               ┌─────────────────────────────┐
                               │  seqNum == expectedSeq_?     │
                               └──┬──────────────────┬────────┘
                               Có │                  │ Không
                                  ▼                  ▼
                        ┌────────────────┐  ┌─────────────────────────┐
                        │  NHẬN GÓI      │  │  GBN: GÓI LỆCH THỨ TỰ  │
                        │  Append data   │  │  Bỏ qua payload          │
                        │  to outData    │  │  Gửi lại ACK(expSeq-1)  │
                        │  expectedSeq_++│  │  Quay lại chờ            │
                        │  Gửi ACK      │  └─────────────────────────┘
                        │  (expSeq-1)   │
                        └───────────────┘
                                │
                        Quay lại đầu vòng lặp
```

---

### Cơ chế ACK Cumulative

Mỗi khi nhận đúng `seqNum == expectedSeq_`:

```table
ackNum = expectedSeq_ - 1   // (trước khi tăng expectedSeq_)
Gửi ACK với:
  - ackNum = ackNum
  - windowSize = advertisedWindow_  (flow control)
  - flags = FLAG_ACK
```

Khi nhận gói sai thứ tự (`seqNum != expectedSeq_`):

- **Bỏ qua payload** (không buffer lại — khác Selective Repeat)
- Gửi lại ACK của gói cuối nhận thành công
- Sender nhận nhiều ACK cũ → timeout → Go-Back-N retransmit từ `base`

---

### So sánh với Stop-and-Wait (backward-compat)

| Tính năng         | Stop-and-Wait (sendPacket) | Sliding Window (sendData)  |
|-------------------|--------------------------- |--------------------------- |
| Gói in-flight     | Tối đa 1                   | Tối đa `effectiveWindow`   |
| Congestion control| Không                      | Có (Slow Start + AIMD)     |
| Flow control      | Không                      | Có (advertisedWindow)      |
| Hiệu năng         | Thấp (mỗi RTT = 1 gói)     | Cao (nhiều gói/RTT)        |
| Dùng khi          | Demo đơn gói nhỏ           | Truyền file thực tế        |

---

## Sơ đồ tương tác Sender — Receiver (Ví dụ thực)

```table
Sender (cwnd=3)                                    Receiver
     │                                                 │
     │──── DATA(seq=0, payloadLen=1024) ─────────────►│
     │──── DATA(seq=1, payloadLen=1024) ─────────────►│
     │──── DATA(seq=2, payloadLen=1024) ─────────────►│
     │                                                 │ Nhận seq=0 OK → ACK(0, win=8)
     │◄──── ACK(ackNum=0, windowSize=8) ──────────────│
     │  cwnd: 1→2 (Slow Start)                        │ Nhận seq=1 OK → ACK(1, win=8)
     │◄──── ACK(ackNum=1, windowSize=8) ──────────────│
     │  cwnd: 2→3                                     │ Nhận seq=2 OK → ACK(2, win=8)
     │◄──── ACK(ackNum=2, windowSize=8) ──────────────│
     │  cwnd: 3→4, slide base=3                       │
     │──── DATA(seq=3, payloadLen=512) ──────────────►│
     │──── DATA(seq=4, payloadLen=1024) ─── [MẤT] ──►│
     │──── DATA(seq=5, payloadLen=1024) ─────────────►│
     │                                                 │ Nhận seq=3 OK → ACK(3, win=8)
     │◄──── ACK(ackNum=3, windowSize=8) ──────────────│
     │                                                 │ Nhận seq=5 (lệch!) → ACK(3, win=8)
     │◄──── ACK(ackNum=3, windowSize=8) ──────────────│
     │                                                 │
     │  [TIMEOUT] ssthresh=4/2=2, cwnd=1              │
     │  Go-Back-N: nextSeq = base = 4                 │
     │──── DATA(seq=4, payloadLen=1024) ─────────────►│
     │──── DATA(seq=5, payloadLen=1024) ─────────────►│
     │                                                 │ Nhận seq=4 OK → ACK(4, win=8)
     │◄──── ACK(ackNum=4, windowSize=8) ──────────────│
     │  cwnd: 1→2 (Slow Start lại)                    │ Nhận seq=5 OK → ACK(5, win=8)
     │◄──── ACK(ackNum=5, windowSize=8) ──────────────│
     │  cwnd: 2→3, base=6 = N → All ACK!             │
     │──── FIN(seq=6) ────────────────────────────────►│
     │◄──── FINACK(FIN+ACK, ackNum=6) ────────────────│
     │  RETURN true                                    │
```
