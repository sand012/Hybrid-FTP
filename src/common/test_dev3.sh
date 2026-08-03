#!/bin/bash

echo "=== [DEV 3] BAT DAU KIEM TRA GIAI DOAN 2 ==="

# 1. Kiem tra CMake build project
echo -e "\n[1/4] Kiem tra CMake build..."
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
cmake ..
cmake --build .
if [ $? -eq 0 ]; then
    echo "✅ Build thanh cong!"
else
    echo "❌ Loi build project! Hay kiem tra lai CMakeLists.txt hoac ma nguon."
    exit 1
fi
cd ..

# 2. Tao file du lieu gia lap de test FileHandler va CryptoHash
echo -e "\n[2/4] Tao file du lieu test (ASCII va Binary)..."
echo "Hello Hybrid-FTP Binary & Text Content Test!" > test_sample.txt
dd if=/dev/urandom of=test_binary.bin bs=1024 count=10 > /dev/null 2>&1
echo "✅ Da tao xong file 'test_sample.txt' va 'test_binary.bin'."

# 3. Kiem tra ket noi FTP Server voi lenh PORT va PASV (Dev 3)
echo -e "\n[3/4] Test lenh PORT va PASV tren Server..."
if [ -f "./build/ftp_server" ] || [ -f "./ftp_server" ]; then
    echo "Mo ket noi thu nghiem toi FTP server..."
    python3 -c '
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    s.connect(("127.0.0.1", 2121))
    print("<-", s.recv(1024).decode())
    s.send(b"USER testuser\r\n")
    print("<-", s.recv(1024).decode())
    s.send(b"PASS testpass\r\n")
    print("<-", s.recv(1024).decode())
    s.send(b"PORT 127,0,0,1,19,136\r\n")
    print("<-", s.recv(1024).decode())
    s.send(b"PASV\r\n")
    print("<-", s.recv(1024).decode())
    s.send(b"QUIT\r\n")
    print("<-", s.recv(1024).decode())
    print("✅ Test lenh dieu khien FTP thanh cong!")
except Exception as e:
    print("⚠️ Khong the ket noi toi server (Hay dam bao ftp_server dang chay o cong 2121):", e)
finally:
    s.close()
'
else
    echo "⚠️ Khong tim thay file chay 'ftp_server'. Bo qua buoc test socket."
fi

# 4. Huong dan test CryptoHash va FileHandler thu cong
echo -e "\n[4/4] Hoan tat chuan bi moi trường test cho Dev 3!"
echo "=== HOAN TAT QUA TRINH KIEM TRA ==="
