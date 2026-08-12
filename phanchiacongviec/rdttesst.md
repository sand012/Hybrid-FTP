# Chứng minh LÍT ĐƠ trách nhầm dev2

## Bước 0 — Build project (bỏ qua nếu đã build)

```bash
cd ~/Hybrid-FTP
rm -rf build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -- -j$(nproc)
ls -lh ftp_server ftp_client   # phải thấy 2 file này

---

## Bước 1 — Đảm bảo không có server cũ đang chiếm cổng 2121

pkill -f ftp_server 2>/dev/null
lsof -i :2121   # nếu còn tiến trình nào, kill nó trước khi qua bước sau

---

## Bước 2 — Symlink thư mục storage (BẮT BUỘC, đây là chỗ fix lỗi)

terminal đang ở Hybrid-FTP/build


rm -rf server_storage
ln -s /tmp/ftp_server_storage server_storage

Sau bước này, mọi file bạn đặt trong /tmp/ftp_server_storage sẽ tự động "xuất hiện"
trong server_storage/ mà server thực sự đọc — không cần sửa code hay đổi thư mục chạy.

---

## Bước 3 — Tạo 3 file test ngẫu nhiên

mkdir -p /tmp/ftp_server_storage
cd /tmp/ftp_server_storage

dd if=/dev/urandom of=file_A.bin bs=512K count=1
dd if=/dev/urandom of=file_B.bin bs=256K count=1
dd if=/dev/urandom of=file_C.bin bs=128K count=1

ls -lh file_A.bin file_B.bin file_C.bin

---

## Bước 4 — Khởi động server (Terminal 1, để chạy nền, đừng đóng)

Đảm bảo đang ở <tên project>

./ftp_server 2>&1 | tee /tmp/server_concurrent_test.log

---

## Bước 5 — Tạo script chạy 3 client song song

Lưu nội dung sau vào /tmp/concurrent_download.sh:

chạy lệnh này:

bashcat > /tmp/concurrent_download.sh << 'EOF'
#!/bin/bash
(
  /mnt/e/HCMUS/MMT/FTP_PROJECT/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user1
pass pass1
binary
passive
retr file_A.bin
quit
COMMANDS
) > /tmp/client1_download.log 2>&1 &

(
  /mnt/e/HCMUS/MMT/FTP_PROJECT/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user2
pass pass2
binary
passive
retr file_B.bin
quit
COMMANDS
) > /tmp/client2_download.log 2>&1 &

(
  /mnt/e/HCMUS/MMT/FTP_PROJECT/build/ftp_client << 'COMMANDS'
open 127.0.0.1 2121
user user3
pass pass3
binary
passive
retr file_C.bin
quit
COMMANDS
) > /tmp/client3_download.log 2>&1 &

wait

echo "=== DOWNLOAD RESULTS ==="
for i in 1 2 3; do
  if grep -q "226 Transfer complete" /tmp/client${i}_download.log; then
    echo "✅ Client $i: Download SUCCESS"
  else
    echo "❌ Client $i: Download FAILED"
  fi
done
EOF


---

## Bước 6 — Chạy script và xem kết quả

chmod +x /tmp/concurrent_download.sh
/tmp/concurrent_download.sh

Kết quả mong đợi:

basch
=== DOWNLOAD RESULTS ===
✅ Client 1: Download SUCCESS
✅ Client 2: Download SUCCESS
✅ Client 3: Download SUCCESS

```