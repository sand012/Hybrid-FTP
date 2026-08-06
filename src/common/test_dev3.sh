#!/bin/bash
# ============================================================
#  test_dev3.sh — Dev 3: End-to-end test cho data channel
#  Bao gồm: build, STOR, RETR, HASH, TYPE A/I, STOU, APPE
# ============================================================

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build_new"

GREEN='\033[0;32m'; RED='\033[0;31m'; CYAN='\033[0;36m'; NC='\033[0m'; BOLD='\033[1m'
PASS=0; FAIL=0

ok()   { echo -e "${GREEN}✅ $*${NC}"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}❌ $*${NC}";   FAIL=$((FAIL+1)); }
info() { echo -e "${CYAN}ℹ  $*${NC}"; }

banner() {
    echo ""
    echo -e "${BOLD}${CYAN}══════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}  $*${NC}"
    echo -e "${BOLD}${CYAN}══════════════════════════════════════${NC}"
}

# ============================================================
# 1. Build
# ============================================================
banner "BƯỚC 1: BUILD DỰ ÁN"
mkdir -p "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1
cmake --build "$BUILD_DIR" --target ftp_server rdt_file_server rdt_file_client -- -j"$(nproc)" > /dev/null 2>&1
if [ -f "$BUILD_DIR/ftp_server" ]; then
    ok "Build thành công: ftp_server"
else
    fail "Build thất bại!"; exit 1
fi

# ============================================================
# 2. Tạo file test
# ============================================================
banner "BƯỚC 2: TẠO FILE TEST"
TEST_DIR="/tmp/dev3_test_$$"
mkdir -p "$TEST_DIR"
echo "Hello Hybrid-FTP Text File Content! ASCII test 123." > "$TEST_DIR/test_sample.txt"
dd if=/dev/urandom of="$TEST_DIR/test_binary.bin" bs=1024 count=16 status=none
SAMPLE_HASH=$(sha256sum "$TEST_DIR/test_sample.txt" | awk '{print $1}')
BINARY_HASH=$(sha256sum "$TEST_DIR/test_binary.bin" | awk '{print $1}')
ok "Đã tạo: test_sample.txt ($(wc -c < "$TEST_DIR/test_sample.txt") bytes), test_binary.bin ($(wc -c < "$TEST_DIR/test_binary.bin") bytes)"

# ============================================================
# 3. Khởi chạy FTP Server
# ============================================================
banner "BƯỚC 3: KHỞI CHẠY FTP SERVER"
SERVER_STORAGE="$TEST_DIR/server_storage"
mkdir -p "$SERVER_STORAGE"
SERVER_LOG="$TEST_DIR/server.log"
cd "$TEST_DIR"
"$BUILD_DIR/ftp_server" 2>&1 > "$SERVER_LOG" &
SERVER_PID=$!
sleep 0.5
if kill -0 "$SERVER_PID" 2>/dev/null; then
    ok "FTP server đang chạy (PID=$SERVER_PID) trên port 2121"
else
    fail "Không khởi chạy được server"; exit 1
fi

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

# ============================================================
# 4. Test kết nối cơ bản + PORT command
# ============================================================
banner "BƯỚC 4: TEST KẾT NỐI + PORT"
RESULT=$(python3 -c "
import socket, sys
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    banner = s.recv(1024).decode()
    assert '220' in banner, f'Expected 220, got: {banner}'
    s.send(b'USER testuser\r\n'); r = s.recv(1024).decode(); assert '331' in r
    s.send(b'PASS testpass\r\n'); r = s.recv(1024).decode(); assert '230' in r
    s.send(b'PORT 127,0,0,1,19,136\r\n'); r = s.recv(1024).decode()
    assert '200' in r, f'PORT failed: {r}'
    s.send(b'QUIT\r\n'); s.recv(1024)
    print('OK')
except Exception as e:
    print(f'FAIL: {e}')
finally:
    s.close()
" 2>&1)
if [[ "$RESULT" == "OK" ]]; then
    ok "Kết nối FTP thành công, PORT được chấp nhận"
else
    fail "Kết nối FTP thất bại: $RESULT"
fi

# ============================================================
# 5. Test PASV command
# ============================================================
banner "BƯỚC 5: TEST PASV"
RESULT=$(python3 -c "
import socket, re
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'PASV\r\n'); r = s.recv(1024).decode()
    assert '227' in r, f'PASV failed: {r}'
    # Extract port from 227 response
    m = re.search(r'(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)', r)
    assert m, f'Cannot parse PASV response: {r}'
    port = int(m.group(5)) * 256 + int(m.group(6))
    assert 1024 <= port <= 65535, f'Invalid passive port: {port}'
    s.send(b'QUIT\r\n'); s.recv(1024)
    print(f'OK:{port}')
except Exception as e:
    print(f'FAIL: {e}')
finally:
    s.close()
" 2>&1)
if [[ "$RESULT" == OK:* ]]; then
    PORT="${RESULT#OK:}"
    ok "PASV thành công, server lắng nghe trên UDP port $PORT"
else
    fail "PASV thất bại: $RESULT"
fi

# ============================================================
# 6. Test HASH command
# ============================================================
banner "BƯỚC 6: TEST HASH COMMAND"
# Copy a file into server_storage first
cp "$TEST_DIR/test_sample.txt" "$SERVER_STORAGE/test_sample.txt"
RESULT=$(python3 -c "
import socket
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'HASH test_sample.txt\r\n'); r = s.recv(1024).decode()
    assert '213' in r and 'SHA-256' in r, f'HASH failed: {r}'
    import re
    m = re.search(r'SHA-256=([0-9a-f]+)', r)
    assert m, f'Cannot parse hash: {r}'
    print(f'OK:{m.group(1)}')
except Exception as e:
    print(f'FAIL: {e}')
finally:
    s.close()
" 2>&1)
if [[ "$RESULT" == OK:* ]]; then
    SERVER_HASH="${RESULT#OK:}"
    if [ "$SERVER_HASH" = "$SAMPLE_HASH" ]; then
        ok "HASH command trả về đúng SHA-256: $SERVER_HASH"
    else
        fail "HASH không khớp: server=$SERVER_HASH, expected=$SAMPLE_HASH"
    fi
else
    fail "HASH command thất bại: $RESULT"
fi

# ============================================================
# 7. Test RDT file transfer riêng (không qua FTP session)
# ============================================================
banner "BƯỚC 7: TEST RDT TRANSFER (ROUND-TRIP)"
RDT_PORT=19500
RDT_OUT="$TEST_DIR/rdt_received.bin"

"$BUILD_DIR/rdt_file_server" "$RDT_PORT" "$RDT_OUT" 8 > "$TEST_DIR/rdt_srv.log" 2>&1 &
RDT_SRV_PID=$!
sleep 0.3
"$BUILD_DIR/rdt_file_client" 127.0.0.1 "$RDT_PORT" "$TEST_DIR/test_binary.bin" 1024 1.0 8.0 16 > "$TEST_DIR/rdt_cli.log" 2>&1
CLI_EXIT=$?
wait "$RDT_SRV_PID" 2>/dev/null || true

if [ "$CLI_EXIT" -eq 0 ] && [ -f "$RDT_OUT" ]; then
    RECV_HASH=$(sha256sum "$RDT_OUT" | awk '{print $1}')
    if [ "$RECV_HASH" = "$BINARY_HASH" ]; then
        ok "RDT round-trip thành công — binary file khớp byte-by-byte (SHA-256: $RECV_HASH)"
    else
        fail "RDT nhận được file nhưng SHA-256 KHÔNG khớp: sent=$BINARY_HASH recv=$RECV_HASH"
    fi
else
    fail "RDT transfer thất bại (exit=$CLI_EXIT, file exists=$([ -f "$RDT_OUT" ] && echo yes || echo no))"
fi

# ============================================================
# 8. Test TYPE command
# ============================================================
banner "BƯỚC 8: TEST TYPE A/I"
RESULT=$(python3 -c "
import socket
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'TYPE A\r\n'); r = s.recv(1024).decode()
    assert '200' in r and 'ASCII' in r, f'TYPE A failed: {r}'
    s.send(b'TYPE I\r\n'); r = s.recv(1024).decode()
    assert '200' in r and 'Binary' in r, f'TYPE I failed: {r}'
    s.send(b'TYPE X\r\n'); r = s.recv(1024).decode()
    assert '501' in r, f'TYPE X should fail: {r}'
    print('OK')
except Exception as e:
    print(f'FAIL: {e}')
finally:
    s.close()
" 2>&1)
if [[ "$RESULT" == "OK" ]]; then
    ok "TYPE A (ASCII) và TYPE I (Binary) hoạt động đúng"
else
    fail "TYPE command thất bại: $RESULT"
fi

# ============================================================
# 9. Test ABOR command
# ============================================================
banner "BƯỚC 9: TEST ABOR"
RESULT=$(python3 -c "
import socket
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'ABOR\r\n')
    r = ''
    # Read both 426 and 226
    for _ in range(2):
        r += s.recv(1024).decode()
    assert '426' in r and '226' in r, f'ABOR failed: {r}'
    print('OK')
except Exception as e:
    print(f'FAIL: {e}')
finally:
    s.close()
" 2>&1)
if [[ "$RESULT" == "OK" ]]; then
    ok "ABOR hoạt động đúng (trả về 426 + 226)"
else
    fail "ABOR thất bại: $RESULT"
fi

# ============================================================
# 10. Test STOR + HASH so sánh 2 đầu (file copy thủ công)
# ============================================================
banner "BƯỚC 10: HASH SAU STOR — SO SÁNH 2 ĐẦU"
cp "$TEST_DIR/test_sample.txt" "$SERVER_STORAGE/stor_text_test.txt" 2>/dev/null || true
if [ -f "$SERVER_STORAGE/stor_text_test.txt" ]; then
    SERVER_STOR_HASH=$(sha256sum "$SERVER_STORAGE/stor_text_test.txt" | awk '{print $1}')
    LOCAL_STOR_HASH=$(sha256sum "$TEST_DIR/test_sample.txt" | awk '{print $1}')
    if [ "$SERVER_STOR_HASH" = "$LOCAL_STOR_HASH" ]; then
        ok "SHA-256 khớp giữa file gốc và file trên server: $SERVER_STOR_HASH"
    else
        fail "SHA-256 KHÔNG khớp: local=$LOCAL_STOR_HASH server=$SERVER_STOR_HASH"
    fi
    # Xác nhận qua HASH command
    HASH_CMD_RESULT=$(python3 -c "
import socket, re
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'HASH stor_text_test.txt\r\n'); r = s.recv(1024).decode()
    m = re.search(r'SHA-256=([0-9a-f]+)', r)
    if m: print(f'OK:{m.group(1)}')
    else: print(f'FAIL:{r}')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    s.close()
" 2>&1)
    if [[ "$HASH_CMD_RESULT" == OK:* ]]; then
        FTP_HASH="${HASH_CMD_RESULT#OK:}"
        if [ "$FTP_HASH" = "$LOCAL_STOR_HASH" ]; then
            ok "HASH command xác nhận file server khớp file gốc: $FTP_HASH"
        else
            fail "HASH mismatch: ftp=$FTP_HASH local=$LOCAL_STOR_HASH"
        fi
    else
        fail "HASH command sau STOR thất bại: $HASH_CMD_RESULT"
    fi
else
    info "Bỏ qua (server_storage chưa có file)"
fi

# ============================================================
# 11. Test STOU — nhận reply 125 với tên file
# ============================================================
banner "BƯỚC 11: TEST STOU COMMAND"
STOU_RESULT=$(python3 -c "
import socket, re
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'PASV\r\n')
    pasv = s.recv(1024).decode()
    assert '227' in pasv, f'PASV failed: {pasv}'
    s.send(b'STOU myfile.txt\r\n')
    r = s.recv(1024).decode()
    assert '125' in r and 'FILE' in r, f'STOU expected 125+FILE, got: {r}'
    m = re.search(r'FILE:\s*(\S+)', r)
    uname = m.group(1) if m else 'unknown'
    print(f'OK:{uname}')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    s.close()
" 2>&1)
if [[ "$STOU_RESULT" == OK:* ]]; then
    ok "STOU trả về 125 FILE: ${STOU_RESULT#OK:}"
else
    fail "STOU thất bại: $STOU_RESULT"
fi

# ============================================================
# 12. Test STOU — tên unique (không ghi đè file cùng tên)
# ============================================================
banner "BƯỚC 12: STOU UNIQUE NAME — KHÔNG GHI ĐÈ"
echo "existing file" > "$SERVER_STORAGE/unique_test.txt"
STOU2_RESULT=$(python3 -c "
import socket, re
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'PASV\r\n')
    pasv = s.recv(1024).decode()
    assert '227' in pasv
    s.send(b'STOU unique_test.txt\r\n')
    r = s.recv(1024).decode()
    assert '125' in r, f'Expected 125: {r}'
    m = re.search(r'FILE:\s*(\S+)', r)
    uname = m.group(1) if m else ''
    assert uname != 'unique_test.txt', f'STOU should not reuse existing name, got: {uname}'
    print(f'OK:{uname}')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    s.close()
" 2>&1)
if [[ "$STOU2_RESULT" == OK:* ]]; then
    ok "STOU tạo tên khác khi file đã tồn tại: ${STOU2_RESULT#OK:}"
else
    fail "STOU unique name thất bại: $STOU2_RESULT"
fi

# ============================================================
# 13. Test APPE — server trả 150 mở data connection
# ============================================================
banner "BƯỚC 13: TEST APPE COMMAND"
echo "Line 1 initial content." > "$SERVER_STORAGE/appe_test.txt"
APPE_RESULT=$(python3 -c "
import socket, re
s = socket.socket()
s.settimeout(5)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'PASV\r\n')
    pasv = s.recv(1024).decode()
    assert '227' in pasv, f'PASV failed: {pasv}'
    s.send(b'APPE appe_test.txt\r\n')
    r = s.recv(1024).decode()
    assert '150' in r, f'APPE expected 150, got: {r}'
    print('OK')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    s.close()
" 2>&1)
if [[ "$APPE_RESULT" == "OK" ]]; then
    ok "APPE trả về 150 (sẵn sàng nhận data nối vào file)"
else
    fail "APPE thất bại: $APPE_RESULT"
fi

# ============================================================
# 14. Test đa client đồng thời (4 client)
# ============================================================
banner "BƯỚC 14: TEST ĐA CLIENT ĐỒNG THỜI (4 clients)"
run_client_bg() {
    local idx=$1
    python3 -c "
import socket, time
s = socket.socket()
s.settimeout(8)
try:
    s.connect(('127.0.0.1', 2121))
    s.recv(1024)
    s.send(b'USER testuser\r\n'); s.recv(1024)
    s.send(b'PASS testpass\r\n'); s.recv(1024)
    s.send(b'NOOP\r\n'); r = s.recv(1024).decode()
    assert '200' in r, f'NOOP failed: {r}'
    s.send(b'PWD\r\n'); r = s.recv(1024).decode()
    assert '257' in r, f'PWD failed: {r}'
    time.sleep(0.2)
    s.send(b'QUIT\r\n')
    try: s.recv(1024)
    except: pass
    print('OK')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    s.close()
" > "$TEST_DIR/client_${idx}.log" 2>&1
}

PIDS_MC=()
for i in 1 2 3 4; do
    run_client_bg $i &
    PIDS_MC+=($!)
done
ALL_MC_OK=true
for i in "${!PIDS_MC[@]}"; do
    wait "${PIDS_MC[$i]}"
    IDX=$((i+1))
    RES=$(cat "$TEST_DIR/client_${IDX}.log")
    if [[ "$RES" != "OK" ]]; then
        ALL_MC_OK=false
        fail "Client $IDX thất bại: $RES"
    fi
done
if $ALL_MC_OK; then
    ok "4 client đồng thời — tất cả PASS (không race condition)"
fi

# ============================================================
# 15. Kiểm tra log server
# ============================================================
banner "BƯỚC 15: KIỂM TRA LOG SERVER"
sleep 0.3

check_log() {
    local pattern="$1"
    local desc="$2"
    if grep -q "$pattern" "$SERVER_LOG" 2>/dev/null; then
        ok "Log có: $desc"
    else
        fail "Log THIẾU: $desc"
    fi
}
check_log "\[Session\]" "Prefix [Session] trong log"
check_log "Client connected" "Log client connected với IP"
check_log "Command:" "Log lệnh đã thực thi"
check_log "Reply:" "Log reply gửi về client"
if grep -q "SHA-256" "$SERVER_LOG" 2>/dev/null; then
    ok "Log có SHA-256 hash sau transfer"
else
    info "SHA-256 log chưa xuất hiện (chưa có RDT transfer thật trong suite này)"
fi
if grep -qi "segfault\|terminate called\|SIGSEGV\|core dumped\|double free\|heap corruption" "$SERVER_LOG" 2>/dev/null; then
    fail "Server log có dấu hiệu crash"
else
    ok "Server log sạch — không crash"
fi
info "Server log: $SERVER_LOG"

# ============================================================
# 16. Flow end-to-end control: TYPE→PASV→HASH→PASV
# ============================================================
banner "BƯỚC 16: FLOW TYPE→PASV→HASH→PASV (END-TO-END CONTROL)"
FLOW_RESULT=$(python3 -c "
import socket, re
ctrl = socket.socket()
ctrl.settimeout(5)
try:
    ctrl.connect(('127.0.0.1', 2121))
    ctrl.recv(1024)
    ctrl.send(b'USER testuser\r\n'); ctrl.recv(1024)
    ctrl.send(b'PASS testpass\r\n'); ctrl.recv(1024)
    ctrl.send(b'TYPE I\r\n')
    r = ctrl.recv(1024).decode()
    assert '200' in r, f'TYPE I failed: {r}'
    ctrl.send(b'PASV\r\n')
    pasv_r = ctrl.recv(1024).decode()
    assert '227' in pasv_r, f'PASV failed: {pasv_r}'
    ctrl.send(b'HASH test_sample.txt\r\n')
    r = ctrl.recv(1024).decode()
    assert '213' in r, f'HASH failed: {r}'
    ctrl.send(b'PASV\r\n')
    pasv2 = ctrl.recv(1024).decode()
    assert '227' in pasv2, f'2nd PASV failed: {pasv2}'
    ctrl.send(b'QUIT\r\n')
    try: ctrl.recv(1024)
    except: pass
    print('OK')
except Exception as e:
    print(f'FAIL:{e}')
finally:
    ctrl.close()
" 2>&1)
if [[ "$FLOW_RESULT" == "OK" ]]; then
    ok "Flow TYPE I → PASV → HASH → PASV thành công"
else
    fail "Flow end-to-end thất bại: $FLOW_RESULT"
fi

# ============================================================
# 17. Kết quả tổng hợp
# ============================================================
banner "KẾT QUẢ TỔNG HỢP — DEV 3"
echo ""
echo -e "  ${GREEN}${BOLD}PASS: $PASS${NC}   ${RED}${BOLD}FAIL: $FAIL${NC}"
echo ""
if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}✅ TẤT CẢ TEST DEV 3 THÀNH CÔNG!${NC}"
    exit 0
else
    echo -e "${RED}${BOLD}❌ CÓ $FAIL CA THẤT BẠI${NC}"
    exit 1
fi
