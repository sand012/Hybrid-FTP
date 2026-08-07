# HYBRID FTP — DỰ ÁN LẬP TRÌNH SOCKET
## Checklist Giai đoạn 4: Tài liệu hóa & Luyện tập bảo vệ (Ngày 11 - 12)

> [!IMPORTANT]
> **Tình trạng các giai đoạn trước (Giai đoạn 1 - 3):**
> * **ĐÃ HOÀN THÀNH 100%**: Tất cả core logic, chế độ Active/Passive, cơ chế truyền tải tin cậy RDT (Sliding Window, Congestion/Flow Control), các lệnh FTP mở rộng (HASH, STOU, APPE, ABOR,...) và xử lý đa luồng (Multi-threading) đã được cài đặt hoàn chỉnh.
> * **KẾT QUẢ KIỂM THỬ**: Hệ thống đã vượt qua toàn bộ các ca kiểm thử tự động trong `test_rdt.sh` (19/19 PASS) và `test_dev3.sh` (22/22 PASS), đảm bảo không xảy ra xung đột dữ liệu hay rò rỉ bộ nhớ.
>
> Do đó, trọng tâm Giai đoạn 4 sẽ tập trung hoàn toàn vào **Tài liệu báo cáo kỹ thuật (Technical Report)** và **Luyện tập vấn đáp / Live Coding** bám sát theo yêu cầu chấm điểm nghiêm ngặt từ giáo viên trong file `Project1_SocketProgramming_2026.docx`.

---

## 1. Phân công Viết Báo cáo Kỹ thuật (Technical Report)
*Báo cáo phải chứa đầy đủ 7 phần bắt buộc dưới đây để tránh bị trừ điểm hoặc đánh trượt học phần.*

### 📝 Dev 1 — Phụ trách chính: Luồng Control & Kiến trúc Tổng quan
- [ ] **Section 1: Application Scenario & Protocol Interaction**
  - Vẽ và mô tả chi tiết biểu đồ trình tự (Sequence Diagram) thể hiện toàn bộ vòng đời kết nối TCP (Control) + UDP (Data) từ lúc khởi tạo đến lúc ngắt kết nối.
- [ ] **Section 2.1: TCP Control Packet Format**
  - Mô tả định dạng các bản tin truyền trên kênh TCP Control, cấu trúc gói lệnh và các mã phản hồi chuẩn (1xx, 2xx, 3xx, 4xx, 5xx) đã cấu hình.
- [ ] **Section 5: Self-Assessment & Peer Evaluation**
  - Tổng hợp đánh giá cá nhân và đóng góp của cả nhóm (phân chia % đóng góp phải đạt tổng cộng 100%).

### 📝 Dev 2 — Phụ trách chính: Reliable UDP & Thuật toán RDT
- [ ] **Section 2.2: UDP Custom Header Fields**
  - Chi tiết từng bit/byte trong cấu trúc header UDP tự định nghĩa (`CustomUDPHeader`): Sequence Number, ACK Number, Checksum, Flags (ACK, FIN, DATA,...), Payload Length, Window Size.
- [ ] **Section 3.2: RDT Sender/Receiver State Machines**
  - Vẽ lưu đồ (Flowchart) mô tả máy trạng thái hữu hạn (FSM) của RDT Window Sender (cơ chế Sliding Window, Slow Start, AIMD) và RDT Window Receiver.
- [ ] **Section 6: GenAI Usage & Code Refinement Log (Mandatory Appendix)**
  - Tổng hợp nhật ký sử dụng AI (Prompt đã dùng, mã nguồn AI sinh ra và cách nhóm đã phân tích lỗi, tối ưu hóa để code chạy an toàn, giải thích chi tiết cơ chế bảo vệ quyền sở hữu trí tuệ).

### 📝 Dev 3 — Phụ trách chính: File I/O, Concurrency & Demo Evidence
- [ ] **Section 3.1: Server Thread-Dispatch Logic & Active/Passive Mode Toggle**
  - Vẽ lưu đồ xử lý đa luồng trên Server và sơ đồ chuyển đổi qua lại giữa chế độ truyền Active (PORT) và Passive (PASV).
- [ ] **Section 4: Task Assignment Matrix**
  - Lập bảng phân công công việc chi tiết từ Giai đoạn 1 đến Giai đoạn 4 (ai code mô-đun nào, ai kiểm thử).
- [ ] **Section 7: Application Demo Evidence (Minh chứng nghiệm thu)**
  - Chụp ảnh màn hình / lưu log chạy thực tế của các ca:
    1. Upload file thành công.
    2. Download file thành công.
    3. So sánh mã băm SHA-256 khớp nhau trước và sau khi truyền.
    4. Bảng danh sách phiên kết nối client hiện tại trên server.
    5. Kiểm thử đồng thời nhiều client tải file cùng lúc.

---

## 2. Kế hoạch Chuẩn bị Vấn đáp & Live Coding (Oral Defense Prep)
*Tiêu chí vấn đáp chiếm tới 50% tổng số điểm (30% Vấn đáp lý thuyết + 20% Live Coding).*

### 🎯 Nội dung ôn tập lý thuyết (Trọng tâm câu hỏi vấn đáp)
- [ ] **Mọi thành viên:** Nắm vững sự khác biệt giữa TCP Control Channel và UDP Data Channel (tại sao phải tách rời, ưu/nhược điểm).
- [ ] **Dev 1:** Giải thích chi tiết luồng xử lý lệnh FTP, cơ chế đa luồng của server, cách phân tích cú pháp lệnh trong `CommandParser`.
- [ ] **Dev 2:** Nắm rõ từng byte của Custom UDP Header, giải thích hoạt động của Sliding Window, cách tính toán RTT, cơ chế tránh tắc nghẽn (Slow Start, AIMD) và cách thu hồi gói tin khi bị mất packet.
- [ ] **Dev 3:** Giải thích cách xử lý file an toàn, tránh ghi đè (STOU), ghi nối (APPE), cách giải quyết đường dẫn tuyệt đối/tương đối (`PathManager`) để tránh lỗ hổng bảo mật thư mục gốc.

### 💻 Kịch bản Diễn tập Live Coding & Debugging (Giáo viên hỏi ngẫu nhiên)
- [ ] **Mô phỏng 1 (Live Bug-fixing):** Giảng viên cố tình tiêm một lỗi mạng (ví dụ: làm tăng tỉ lệ mất gói tin UDP hoặc ngắt kết nối TCP đột ngột), yêu cầu nhóm tìm ra dòng code gây lỗi và sửa trực tiếp tại chỗ.
- [ ] **Mô phỏng 2 (Sửa tham số cấu hình RDT):** Thay đổi kích thước cửa sổ (`windowSize`), đổi thuật toán RDT từ Sliding Window về Stop-and-Wait để xem tốc độ thay đổi như thế nào.
- [ ] **Mô phỏng 3 (Thêm tính năng nhỏ):** Giảng viên yêu cầu viết thêm một lệnh mới đơn giản live trên control channel (ví dụ: in thêm lời chào hoặc thay đổi định dạng log hiển thị).

---

## 3. Checklist Kiểm tra trước khi nộp bài (Demo & Submission Checklist)
- [ ] Dự án biên dịch sạch sẽ không có lỗi/cảnh báo nghiêm trọng trên máy sạch (clean machine):
  ```bash
  rm -rf build && cmake -B build && cmake --build build
  ```
- [ ] Đã chạy lại script `test_rdt.sh` và `test_dev3.sh` trên môi trường nghiệm thu đạt kết quả PASS 100%.
- [ ] Tài liệu báo cáo kỹ thuật định dạng `.docx` hoặc `.pdf` đã tích hợp đầy đủ 7 phần nêu trên.
- [ ] Tất cả mã nguồn đã được commit lên Git với lịch sử commit rõ ràng, thể hiện sự phát triển mã nguồn của từng thành viên.
