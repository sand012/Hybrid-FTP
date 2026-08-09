# Section 3.1: Server Thread-Dispatch Logic & Active/Passive Mode Toggle

> **Người phụ trách:** Dev 3  
> **File liên quan:** `src/server/ServerManager.cpp`, `src/server/Session.cpp`, `src/server/Session.h`

---

## 3.1.1 Kiến trúc xử lý đa luồng trên Server (Thread-Dispatch Logic)

### Tổng quan

Hybrid FTP Server sử dụng mô hình **Thread-per-Client** (mỗi client một luồng riêng biệt) dựa trên POSIX Threads (`pthread`). Khi một client kết nối tới, server sinh ra một thread mới chạy hàm `handle_client_thread()` và ngay lập tức `detach` thread đó để quay lại vòng lặp `accept()` phục vụ client tiếp theo.

### Cấu trúc dữ liệu quản lý phiên

```cpp
#define MAX_CLIENTS 100

struct ClientRecord {
    int socketFd;     // File descriptor của TCP socket
    char ip[46];      // Địa chỉ IP client (hỗ trợ cả IPv6)
    int port;         // Port TCP của client
    int active;       // 1 = đang hoạt động, 0 = slot trống
};

struct ServerState {
    int listenFd;                        // Socket lắng nghe TCP
    int port;                            // Port mà server bind
    int running;                         // Cờ trạng thái server (1=chạy, 0=dừng)
    pthread_mutex_t clientsLock;         // Mutex bảo vệ bảng clients
    ClientRecord clients[MAX_CLIENTS];   // Bảng lưu trữ tối đa 100 phiên
};
```

### Lưu đồ xử lý đa luồng (Thread-Dispatch Flowchart)

```mermaid
flowchart TD
    A["Server khởi động<br/><code>server_init()</code>"] --> B["Tạo TCP Socket<br/><code>socket(AF_INET, SOCK_STREAM, 0)</code>"]
    B --> C["Bind cổng TCP<br/><code>bind(INADDR_ANY, port)</code>"]
    C --> D["Bắt đầu lắng nghe<br/><code>listen(fd, backlog=32)</code>"]
    D --> E["Vòng lặp Accept<br/><code>server_run()</code>"]

    E --> F{"<code>accept()</code><br/>Client mới kết nối?"}
    F -- "Có" --> G["Lấy IP/Port client<br/><code>inet_ntop()</code>"]
    G --> H["Thêm vào bảng sessions<br/><code>clients_add()</code><br/>🔒 pthread_mutex_lock"]
    H --> I["Cấp phát <code>SessionArgs</code><br/>trên heap (malloc)"]
    I --> J["Tạo thread mới<br/><code>pthread_create()</code><br/>→ <code>handle_client_thread</code>"]
    J --> K["Detach thread<br/><code>pthread_detach(tid)</code>"]
    K --> E

    F -- "Lỗi / Server dừng" --> L["Kiểm tra <code>state→running</code>"]
    L -- "running = 0" --> M["Thoát vòng lặp"]
    L -- "running = 1" --> E

    J -.-> N["🧵 Thread con (1 per Client)"]

    subgraph ThreadCon["🧵 Thread xử lý Client — handle_client_thread()"]
        N --> O["Khởi tạo <code>SessionState</code><br/>(biến cục bộ trên stack)"]
        O --> P["Gửi banner chào mừng<br/><code>220 Hybrid FTP service ready.</code>"]
        P --> Q["Đọc dòng lệnh<br/><code>read_line(fd)</code>"]
        Q --> R{"Dòng hợp lệ?"}
        R -- "Có" --> S["Phân tích & xử lý lệnh<br/><code>handle_command()</code>"]
        S --> T{"Lệnh QUIT<br/>hoặc lỗi?"}
        T -- "Không" --> Q
        T -- "Có (QUIT)" --> U["Dọn dẹp tài nguyên"]
        R -- "Client ngắt kết nối<br/>(recv = 0 hoặc -1)" --> U
        U --> V["Đóng TCP socket<br/><code>shutdown() + close()</code>"]
        V --> W["Xóa khỏi bảng sessions<br/><code>clients_remove()</code><br/>🔒 pthread_mutex_lock"]
        W --> X["Giải phóng bộ nhớ<br/><code>free(args)</code>"]
        X --> Y["Thread kết thúc"]
    end

    style A fill:#1a1a2e,stroke:#e94560,color:#eee
    style E fill:#16213e,stroke:#0f3460,color:#eee
    style J fill:#0f3460,stroke:#533483,color:#eee
    style N fill:#533483,stroke:#e94560,color:#eee
    style ThreadCon fill:#0d1117,stroke:#533483,color:#eee
```

### Cơ chế bảo vệ dữ liệu chia sẻ (Thread Safety)

| Tài nguyên chia sẻ | Cơ chế bảo vệ | Giải thích |
| :--- | :--- | :--- |
| `ServerState::clients[]` | `pthread_mutex_t clientsLock` | Mutex lock/unlock mỗi khi thêm (`clients_add`) hoặc xóa (`clients_remove`) client khỏi bảng phiên |
| `SessionState` | Biến cục bộ trên stack của thread | Mỗi thread có bản sao `SessionState` riêng → không cần đồng bộ |
| `SessionArgs` | Cấp phát trên heap, 1 owner duy nhất | `malloc()` trong `server_run()`, `free()` trong `handle_client_thread()` → không race condition |
| TCP socket `fd` | Mỗi thread sở hữu riêng 1 fd | `accept()` trả về fd duy nhất cho mỗi kết nối |

### Ưu điểm của mô hình Thread-per-Client

1. **Đơn giản về logic**: Mỗi client được cách ly hoàn toàn trong thread riêng, không cần multiplexing phức tạp (`select`/`poll`/`epoll`).
2. **Blocking I/O tự nhiên**: Các lệnh truyền file (RETR/STOR) có thể block mà không ảnh hưởng client khác.
3. **SessionState cục bộ**: Trạng thái đăng nhập, thư mục làm việc, chế độ truyền đều là biến cục bộ → tránh hoàn toàn xung đột dữ liệu giữa các client.

### Hạn chế và hướng cải tiến

- Số client tối đa bị giới hạn bởi `MAX_CLIENTS = 100` và số thread hệ điều hành cho phép.
- Mỗi thread tiêu tốn ~1–8 MB stack → nếu cần phục vụ hàng nghìn client đồng thời, nên chuyển sang mô hình event-driven (epoll + thread pool).

---

## 3.1.2 Sơ đồ chuyển đổi Active/Passive Mode

### Tổng quan hai chế độ truyền dữ liệu

Hybrid FTP hỗ trợ hai chế độ mở kênh dữ liệu UDP:

| Đặc điểm | Active Mode (PORT) | Passive Mode (PASV) |
| :--- | :--- | :--- |
| **Ai mở socket UDP?** | Server tạo socket mới, gửi tới IP:Port client chỉ định | Server tạo socket mới, bind port ngẫu nhiên, chờ client kết nối tới |
| **Hướng khởi tạo kết nối** | Server → Client | Client → Server |
| **Lệnh FTP** | `PORT h1,h2,h3,h4,p1,p2` | `PASV` |
| **Mã phản hồi** | `200 PORT command successful.` | `227 Entering Passive Mode (h1,h2,h3,h4,p1,p2).` |
| **Phù hợp khi** | Client không nằm sau NAT/firewall | Client nằm sau NAT/firewall (phổ biến hơn) |
| **Số lần sử dụng** | Có thể tái sử dụng cho nhiều transfer | Mỗi lần transfer phải gọi PASV lại (single-use) |

### Trạng thái data channel trong SessionState

```cpp
enum class DataMode { NONE, ACTIVE, PASSIVE };

// Trong struct SessionState:
DataMode dataMode = DataMode::NONE;          // Chế độ hiện tại
std::string activeIP;                        // IP client (Active mode)
uint16_t activePort = 0;                     // UDP port client (Active mode)
std::unique_ptr<UDPSocket> passiveSocket;    // Socket UDP server bind (Passive mode)
```

### Sơ đồ trạng thái chuyển đổi Active/Passive Mode

```mermaid
stateDiagram-v2
    [*] --> NONE: Khởi tạo SessionState

    NONE --> ACTIVE: Nhận lệnh PORT\n→ Lưu activeIP, activePort\n→ Reply 200
    NONE --> PASSIVE: Nhận lệnh PASV\n→ Tạo UDPSocket, bind(0)\n→ Reply 227

    ACTIVE --> ACTIVE: Nhận lệnh PORT mới\n→ Cập nhật IP/Port
    ACTIVE --> PASSIVE: Nhận lệnh PASV\n→ Tạo UDPSocket mới\n→ Reply 227
    ACTIVE --> NONE: Transfer hoàn thành\n(Active socket tạm thời bị xóa)

    PASSIVE --> ACTIVE: Nhận lệnh PORT\n→ Đóng passiveSocket\n→ Lưu IP/Port mới
    PASSIVE --> PASSIVE: Nhận lệnh PASV mới\n→ Đóng socket cũ\n→ Tạo socket mới
    PASSIVE --> NONE: Transfer hoàn thành\n→ passiveSocket.reset()\n→ dataMode = NONE

    ACTIVE --> TRANSFER_A: RETR / STOR / LIST\n→ Tạo UDPSocket tạm\n→ Gửi/Nhận RDT tới client
    PASSIVE --> TRANSFER_P: RETR / STOR / LIST\n→ Chờ knock từ client\n→ Học peerIP/peerPort

    TRANSFER_A --> NONE: Transfer xong\n→ Đóng socket tạm
    TRANSFER_P --> NONE: Transfer xong\n→ passiveSocket.reset()

    state TRANSFER_A {
        [*] --> SendOrRecv_A
        SendOrRecv_A: RDTSender/Receiver\nGửi/Nhận file qua UDP
    }

    state TRANSFER_P {
        [*] --> WaitKnock
        WaitKnock: Chờ datagram đầu tiên\ntừ client (learn IP/Port)
        WaitKnock --> SendOrRecv_P
        SendOrRecv_P: RDTSender/Receiver\nGửi/Nhận file qua UDP
    }
```

### Lưu đồ chi tiết: Mở Data Channel cho một lệnh Transfer

```mermaid
flowchart TD
    START["Nhận lệnh transfer<br/>(RETR / STOR / STOU / APPE / LIST / NLST)"] --> CHECK{"Kiểm tra<br/><code>session.dataMode</code>"}

    CHECK -- "NONE" --> ERR1["Reply <code>425 Use PORT or PASV first.</code><br/>❌ Kết thúc"]

    CHECK -- "ACTIVE" --> A1["Kiểm tra activeIP/activePort hợp lệ"]
    A1 -- "Không hợp lệ" --> ERR1
    A1 -- "Hợp lệ" --> A2["Tạo UDPSocket tạm thời<br/><code>new UDPSocket()</code>"]
    A2 --> A3["<code>sock→open()</code><br/><code>sock→bind(0)</code>"]
    A3 -- "Thất bại" --> ERR2["Reply <code>425 Cannot open data connection.</code><br/>❌ Kết thúc"]
    A3 -- "Thành công" --> A4["Thiết lập peerIP/peerPort<br/>từ session.activeIP/activePort"]
    A4 --> TRANSFER

    CHECK -- "PASSIVE" --> P1["Lấy <code>session.passiveSocket</code>"]
    P1 -- "passiveSocket == nullptr" --> ERR3["Reply <code>425 No passive socket available.</code><br/>❌ Kết thúc"]
    P1 -- "Có socket" --> P2["<code>recvFrom()</code> — Chờ knock<br/>từ client (timeout 10s)"]
    P2 -- "Timeout / Lỗi" --> ERR4["Reply <code>425 No connection from client.</code><br/>❌ Kết thúc"]
    P2 -- "Nhận được datagram" --> P3["Học được <code>peerIP</code> và <code>peerPort</code><br/>của client từ datagram đầu tiên"]
    P3 --> TRANSFER

    TRANSFER["Gửi reply <code>150 Opening data connection...</code>"] --> RDT{"Loại transfer?"}

    RDT -- "RETR / LIST / NLST<br/>(Server → Client)" --> SEND["Tạo <code>RDTSender(socket, peerIP, peerPort)</code><br/>Gọi <code>sendFile()</code> hoặc <code>sendBuffer()</code>"]
    RDT -- "STOR / STOU / APPE<br/>(Client → Server)" --> RECV["Tạo <code>RDTReceiver(socket)</code><br/>Gọi <code>receiveBuffer()</code>"]

    SEND --> RESULT
    RECV --> WRITE["Ghi dữ liệu ra đĩa<br/><code>FileHandler::writeBinaryFile()</code><br/>hoặc <code>writeTextFile()</code>"]
    WRITE --> RESULT

    RESULT{"Kết quả?"}
    RESULT -- "Thành công" --> OK["Tính SHA-256 và log<br/>Reply <code>226 Transfer complete.</code><br/>✅"]
    RESULT -- "Bị hủy (ABOR)" --> ABORTED["Reply <code>426 Connection closed.</code><br/>Reply <code>226 Abort successful.</code>"]
    RESULT -- "Lỗi mạng" --> FAIL["Reply <code>426 Connection closed; transfer aborted.</code>"]

    OK --> CLEANUP
    ABORTED --> CLEANUP
    FAIL --> CLEANUP

    CLEANUP["Dọn dẹp:<br/>Active → <code>delete socket</code><br/>Passive → <code>passiveSocket.reset()</code><br/><code>dataMode = NONE</code>"]

    style START fill:#1a1a2e,stroke:#e94560,color:#eee
    style TRANSFER fill:#16213e,stroke:#0f3460,color:#eee
    style OK fill:#0d3b0d,stroke:#2ea043,color:#eee
    style ERR1 fill:#3b0d0d,stroke:#e94560,color:#eee
    style ERR2 fill:#3b0d0d,stroke:#e94560,color:#eee
    style ERR3 fill:#3b0d0d,stroke:#e94560,color:#eee
    style ERR4 fill:#3b0d0d,stroke:#e94560,color:#eee
    style FAIL fill:#3b0d0d,stroke:#e94560,color:#eee
```

### Luồng tương tác Active Mode vs Passive Mode (Sequence Diagram)

```mermaid
sequenceDiagram
    box rgb(13,17,23) Active Mode
    participant CA as FTP Client
    participant SA as FTP Server
    end

    Note over CA,SA: ═══ ACTIVE MODE ═══

    CA->>SA: PORT 192,168,1,10,200,15 (TCP)
    Note right of SA: Lưu activeIP=192.168.1.10<br/>activePort=51215<br/>dataMode=ACTIVE
    SA-->>CA: 200 PORT command successful. (TCP)

    CA->>SA: STOR myfile.bin (TCP)
    SA-->>CA: 150 Opening data connection... (TCP)
    Note right of SA: Server tạo UDPSocket mới<br/>bind(0) → ephemeral port<br/>Gửi/Nhận RDT tới 192.168.1.10:51215

    rect rgb(15,52,96)
        CA->>SA: 📦 RDT DATA packets (UDP)
        SA-->>CA: ✅ RDT ACK packets (UDP)
        CA->>SA: 📦 FIN packet (UDP)
        SA-->>CA: ✅ FINACK (UDP)
    end

    Note right of SA: Ghi file, tính SHA-256<br/>Đóng socket tạm, xóa
    SA-->>CA: 226 Transfer complete. (TCP)

    Note over CA,SA: ═══════════════════════

    box rgb(13,17,23) Passive Mode
    participant CP as FTP Client
    participant SP as FTP Server
    end

    Note over CP,SP: ═══ PASSIVE MODE ═══

    CP->>SP: PASV (TCP)
    Note right of SP: Tạo UDPSocket<br/>bind(0) → port 54321<br/>dataMode=PASSIVE
    SP-->>CP: 227 Entering Passive Mode (127,0,0,1,212,49). (TCP)

    CP->>SP: RETR document.txt (TCP)
    Note left of CP: Client gửi knock<br/>(datagram 15 byte rỗng)

    rect rgb(83,52,131)
        CP->>SP: 🤝 Knock datagram (UDP → port 54321)
        Note right of SP: Học peerIP/peerPort<br/>từ knock datagram
    end

    SP-->>CP: 150 Opening data connection... (TCP)

    rect rgb(15,52,96)
        SP->>CP: 📦 RDT DATA packets (UDP)
        CP-->>SP: ✅ RDT ACK packets (UDP)
        SP->>CP: 📦 FIN packet (UDP)
        CP-->>SP: ✅ FINACK (UDP)
    end

    Note right of SP: Tính SHA-256, log<br/>passiveSocket.reset()<br/>dataMode=NONE
    SP-->>CP: 226 Transfer complete. (TCP)
```

### So sánh luồng xử lý trong code

#### Active Mode — `open_data_channel` lambda (Session.cpp)

```cpp
// Khi dataMode == ACTIVE:
auto *sock = new UDPSocket();     // 1. Tạo socket UDP tạm thời
sock->open();                      // 2. Mở socket
sock->bind(0);                     // 3. Bind port ngẫu nhiên
dataSocket = sock;                 // 4. Trả về cho caller
ownsSocket = true;                 // 5. Caller chịu trách nhiệm delete
peerIP = session.activeIP;         // 6. Đích đến = IP client đã gửi qua PORT
peerPort = session.activePort;     // 7. Port đích = Port client đã gửi qua PORT
```

#### Passive Mode — `open_data_channel` lambda (Session.cpp)

```cpp
// Khi dataMode == PASSIVE:
dataSocket = session.passiveSocket.get();  // 1. Dùng socket đã bind từ lệnh PASV
ownsSocket = false;                         // 2. Session sở hữu socket, không delete
peerIP = "";                                // 3. Chưa biết IP client
peerPort = 0;                               // 4. Chưa biết port client
// → Client phải gửi knock datagram trước, server recvFrom() để học IP/port
```

### Cơ chế dọn dẹp sau transfer

| Chế độ | Sau khi transfer xong | Lý do |
| :--- | :--- | :--- |
| **Active** | `dataSocket->close(); delete dataSocket;` | Socket được tạo mới cho mỗi transfer, cần giải phóng |
| **Passive** | `session.passiveSocket.reset(); session.dataMode = DataMode::NONE;` | Socket passive là single-use theo chuẩn FTP, cần PASV lại cho transfer tiếp theo |

---

## 3.1.3 Tóm tắt kiến trúc tổng thể

```mermaid
flowchart LR
    subgraph ClientSide["🖥️ FTP Client"]
        CLI["ClientCLI<br/>ftp> prompt"]
        CUDP["UDPSocket<br/>(Data Channel)"]
    end

    subgraph ServerSide["🖧 FTP Server"]
        SM["ServerManager<br/>(Accept Loop)"]
        subgraph Thread1["🧵 Thread Client 1"]
            S1["Session #1<br/>SessionState"]
            DC1["Data Channel<br/>UDPSocket"]
        end
        subgraph Thread2["🧵 Thread Client 2"]
            S2["Session #2<br/>SessionState"]
            DC2["Data Channel<br/>UDPSocket"]
        end
        subgraph ThreadN["🧵 Thread Client N"]
            SN["Session #N<br/>SessionState"]
            DCN["Data Channel<br/>UDPSocket"]
        end
    end

    CLI -- "TCP Control Channel<br/>(Lệnh FTP + Reply)" --> SM
    SM -- "pthread_create" --> Thread1
    SM -- "pthread_create" --> Thread2
    SM -- "pthread_create" --> ThreadN

    CUDP <-- "UDP Data Channel<br/>(RDT Sliding Window)" --> DC1

    style ClientSide fill:#0d1117,stroke:#58a6ff,color:#eee
    style ServerSide fill:#0d1117,stroke:#e94560,color:#eee
    style Thread1 fill:#161b22,stroke:#533483,color:#eee
    style Thread2 fill:#161b22,stroke:#533483,color:#eee
    style ThreadN fill:#161b22,stroke:#533483,color:#eee
```

**Kết luận:** Kiến trúc đa luồng Thread-per-Client kết hợp với hai chế độ truyền Active/Passive cho phép Hybrid FTP Server phục vụ đồng thời nhiều client, mỗi client có thể chọn chế độ truyền phù hợp với cấu hình mạng của mình. Cơ chế mutex bảo vệ bảng phiên kết nối đảm bảo không xảy ra race condition khi thêm/xóa client.
