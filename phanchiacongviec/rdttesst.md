  ### BƯỚC 1: Chuẩn Bị Kết Nối Mạng Giữa 2 Máy

  Hai máy Linux cần nằm trong cùng một mạng (chung mạng Wi-Fi/LAN, hoặc dùng mạng ảo như
  Tailscale/ZeroTier, hoặc 2 máy ảo Bridged).

  #### 1. Lấy địa chỉ IP máy Server (Máy của bạn)

  Mở Terminal trên máy Server và chạy:

    hostname -I | awk '{print $1}'
    # Giả sử IP hiển thị là: 192.168.1.50 (ký hiệu là <SERVER_IP>)

  #### 2. Cấu hình Firewall (Máy của bạn - Server)

  Vì dự án FTP lai sử dụng TCP 2121 (Control) và các port UDP ngẫu nhiên (RDT Data Channel),
  hãy mở port hoặc tắt tạm firewall trong lúc demo:

    # Cách 1: Cho phép port control và UDP
    sudo ufw allow 2121/tcp
    sudo ufw allow 1024:65535/udp
    # Hoặc Cách 2 (tạm tắt nếu test nội bộ an toàn):
    sudo ufw disable

  #### 3. Kiểm tra kết nối từ máy Client (Máy bạn của bạn)

  Trên máy Client, kiểm tra ping tới máy Server:

    ping -c 4 <SERVER_IP>

  (Nếu ping thành công là 2 máy đã thông mạng).
  ──────
  ### BƯỚC 2: Biên Dịch Dự Án Trên Cả 2 Máy

  Thực hiện lệnh sau trên cả 2 máy (trong thư mục dự án):

    cd /mnt/e/HCMUS/MMT/FTP_PROJECT   # Hoặc đường dẫn thư mục project trên mỗi máy
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . -- -j$(nproc)

  Kiểm tra:

  • Trên máy Server có file thực thi: ./ftp_server
  • Trên máy Client có file thực thi: ./ftp_client
  ──────
  ### BƯỚC 3: Hướng Dẫn Thực Hiện 7 Ca Test Trong Section 7

  │ Lưu ý khác biệt lớn nhất so với chạy local: Trong các lệnh của Client, thay 127.0.0.1
  │ bằng <SERVER_IP> thực tế của máy bạn (ví dụ 192.168.1.50).
  ──────
  #### 🧪 Ca Kiểm Thử 1: Upload File (STOR + RDT)

  1. Trên máy Client (Chuẩn bị file binary 1MB cần upload):
    mkdir -p /tmp/ftp_client_storage && cd /tmp/ftp_client_storage
    dd if=/dev/urandom of=sample_binary.bin bs=1M count=1
    sha256sum sample_binary.bin > sample_binary.sha256

  2. Trên máy Server (Khởi động Server):
    cd ~/Hybrid-FTP/build # hoặc thư mục build của bạn
    ./ftp_server

  3. Trên máy Client (Tiến hành kết nối và Upload):
    cd /tmp/ftp_client_storage
    ~/Hybrid-FTP/build/ftp_client
  Nhập các lệnh:
    open <SERVER_IP> 2121
    user testuser
    pass testpass
    binary
    passive
    stor sample_binary.bin
    quit

  4. Kiểm tra (Verification):
      • Trên Server: Kiểm tra trong thư mục server_storage/ đã xuất hiện sample_binary.bin
      với kích thước 1,048,576 bytes.

  ──────
  #### 🧪 Ca Kiểm Thử 2: Download File (RETR + RDT)

  1. Trên máy Server (Đảm bảo file đã có trong server_storage/):
    ls -lh server_storage/sample_binary.bin

  2. Trên máy Client (Xóa file cũ ở Client để test tải về):
    cd /tmp/ftp_client_storage
    rm -f sample_binary.bin
    ~/Hybrid-FTP/build/ftp_client
  Nhập các lệnh:
    open <SERVER_IP> 2121
    user testuser
    pass testpass
    binary
    passive
    retr sample_binary.bin
    quit

  3. Kiểm tra (Verification):
      • Trên máy Client: ls -lh sample_binary.bin xem file đã được tải về đầy đủ 1MB.

  ──────
  #### 🧪 Ca Kiểm Thử 3: So Sánh Mã Băm SHA-256 (Integrity Check)

  1. Trên máy Server (Lấy hash file gốc):
    sha256sum server_storage/sample_binary.bin

  2. Trên máy Client (Kiểm tra hash từ lệnh HASH của FTP Client):
    ~/Hybrid-FTP/build/ftp_client

    open <SERVER_IP> 2121
    user testuser
    pass testpass
    hash sample_binary.bin
    quit

  3. So sánh: Mã SHA-256 từ lệnh hash trả về từ server và mã SHA-256 tính trực tiếp trên
  Client sha256sum /tmp/ftp_client_storage/sample_binary.bin phải khớp 100%.
  ──────
  #### 🧪 Ca Kiểm Thử 4: Quản Lý Phiên Kết Nối (Session Tracking)

  1. Trên máy Server: Giữ Server đang chạy để quan sát log kết nối và Thread ID.
  2. Trên máy Client: Mở 3 terminal riêng biệt và cùng kết nối tới Server:
      • Terminal Client 1:
        ~/Hybrid-FTP/build/ftp_client
        # open <SERVER_IP> 2121 -> user testuser1 -> pass pass1 -> pwd

      • Terminal Client 2:
        ~/Hybrid-FTP/build/ftp_client
        # open <SERVER_IP> 2121 -> user testuser2 -> pass pass2 -> list

      • Terminal Client 3:
        ~/Hybrid-FTP/build/ftp_client
        # open <SERVER_IP> 2121 -> user testuser3 -> pass pass3 -> binary

  3. Minh chứng: Terminal của Server sẽ in ra các log kết nối kèm IP của máy Client cùng các
  Port khác nhau ứng với từng Thread được dispatch độc lập.
  ──────
  #### 🧪 Ca Kiểm Thử 5: Tải Đồng Thời Nhiều Client (Concurrent Downloads)

  1. Trên máy Server: Chuẩn bị 3 file mẫu:
    mkdir -p server_storage
    dd if=/dev/urandom of=server_storage/file_A.bin bs=512K count=1
    dd if=/dev/urandom of=server_storage/file_B.bin bs=256K count=1
    dd if=/dev/urandom of=server_storage/file_C.bin bs=128K count=1

  2. Trên máy Client: Mở 3 terminal và chạy đồng thời để tải 3 file:
      • Terminal 1: retr file_A.bin
      • Terminal 2: retr file_B.bin
      • Terminal 3: retr file_C.bin
  3. Minh chứng: Cả 3 file truyền tải song song qua RDT UDP mà không xung đột port/dữ liệu.
  ──────
  #### 🧪 Ca Kiểm Thử 6 & 7: Chế Độ ASCII (TYPE A) và Binary (TYPE I)

  • Test ASCII (TYPE A):
      • Tạo file readme.txt trên máy Client.
      • Chạy ftp_client, gõ:
        open <SERVER_IP> 2121
        user testuser
        pass testpass
        type A
        passive
        stor readme.txt
        quit

  • Test Binary (TYPE I):
      • Chạy ftp_client, gõ:
        open <SERVER_IP> 2121
        user testuser
        pass testpass
        type I
        passive
        stor sample_binary.bin
        quit


  ──────
  ### 💡 Mẹo khi chụp Screenshot Báo Cáo Section 7

  • Bật chia đôi màn hình (Split terminal) hoặc chụp cả cửa sổ Terminal máy Server (hiển thị
  IP server) và Terminal máy Client (hiển thị lệnh open <SERVER_IP> 2121).
  • Điều này chứng minh minh bạch rằng hệ thống hoạt động thực tế qua giao tiếp mạng giữa 2
  máy vật lý/môi trường Linux riêng biệt.