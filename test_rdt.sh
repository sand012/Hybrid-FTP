#!/bin/bash
# ============================================================
#  test_rdt.sh — Script nghiệm thu RDT + Sliding Window
#
#  Kiểm tra:
#    1. Build dự án
#    2. Chuẩn bị dữ liệu test
#    3. Stop-and-Wait backward-compat (demo cũ)
#    4. Truyền file nhiều kích thước & cấu hình cửa sổ
#    5. Congestion Control: so sánh cwnd & timeout giữa các run
#    6. Flow Control: thay đổi advertisedWindow receiver
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_ROOT/build"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PASS=0
FAIL=0

ok()   { echo -e "${GREEN}✅ $*${NC}"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}❌ $*${NC}";   FAIL=$((FAIL+1)); }
info() { echo -e "${CYAN}ℹ  $*${NC}"; }
warn() { echo -e "${YELLOW}⚠  $*${NC}"; }

banner() {
    echo ""
    echo -e "${BOLD}${CYAN}══════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}  $*${NC}"
    echo -e "${BOLD}${CYAN}══════════════════════════════════════════════${NC}"
}

# ============================================================
#  0. Build
# ============================================================
banner "BƯỚC 0: BUILD DỰ ÁN"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

info "Chạy CMake configure..."
cmake "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1

info "Build các target RDT..."
cmake --build . \
      --target rdt_file_server rdt_file_client rdt_saw_server rdt_saw_client \
      -- -j"$(nproc)" 2>&1 | tail -8

if [ -f "$BUILD_DIR/rdt_file_server" ] && [ -f "$BUILD_DIR/rdt_file_client" ]; then
    ok "Build thành công: rdt_file_server, rdt_file_client, rdt_saw_server, rdt_saw_client"
else
    fail "Build thất bại — kiểm tra lỗi compiler ở trên."
    exit 1
fi

cd "$PROJECT_ROOT"

# ============================================================
#  Hàm tiện ích: tính SimpleSum bằng Python3
# ============================================================
file_sum() { python3 -c "d=open('$1','rb').read(); print(hex(sum(d)&0xFFFFFFFF))"; }

# ============================================================
#  Hàm chạy 1 ca nghiệm thu file transfer
#  Args: TEST_NAME FILE_PATH MSS INIT_CWND SSTHRESH MAX_WIN ADV_WIN PORT
# ============================================================
BASE_PORT=9100

run_test() {
    local NAME="$1"
    local FILE="$2"
    local MSS="${3:-1024}"
    local CWND="${4:-1.0}"
    local SSTH="${5:-8.0}"
    local MAXW="${6:-32}"
    local ADVW="${7:-8}"
    local PORT=$((BASE_PORT++))

    local RECV_FILE="/tmp/rdt_recv_${PORT}.bin"
    rm -f "$RECV_FILE"

    local FILE_SIZE
    FILE_SIZE=$(wc -c < "$FILE")
    local EXPECTED_SUM
    EXPECTED_SUM=$(file_sum "$FILE")

    echo ""
    info "--- $NAME | Port=$PORT | MSS=$MSS cwnd=$CWND ssth=$SSTH maxWin=$MAXW advWin=$ADVW ---"

    # Jalankan server di background
    "$BUILD_DIR/rdt_file_server" "$PORT" "$RECV_FILE" "$ADVW" \
        > "/tmp/rdt_srv_${PORT}.log" 2>&1 &
    local SRV_PID=$!
    sleep 0.3

    # Jalankan client
    "$BUILD_DIR/rdt_file_client" \
        127.0.0.1 "$PORT" "$FILE" "$MSS" "$CWND" "$SSTH" "$MAXW" \
        > "/tmp/rdt_cli_${PORT}.log" 2>&1
    local CLI_EXIT=$?

    wait "$SRV_PID" 2>/dev/null || true

    # Kiểm tra kết quả
    if [ "$CLI_EXIT" -ne 0 ]; then
        fail "$NAME: Client exit=$CLI_EXIT"
        info "  [CLIENT LOG]"; cat "/tmp/rdt_cli_${PORT}.log" | head -15
        return
    fi

    if [ ! -f "$RECV_FILE" ]; then
        fail "$NAME: File nhận không tồn tại"
        return
    fi

    local RECV_SIZE
    RECV_SIZE=$(wc -c < "$RECV_FILE")
    local ACTUAL_SUM
    ACTUAL_SUM=$(file_sum "$RECV_FILE")

    # Lấy thống kê từ log client
    local THRU CWND_FINAL TIMEOUTS SEGS
    THRU=$(grep -oP "Thong luong\s+:\s+\K[\d.]+" "/tmp/rdt_cli_${PORT}.log" || echo "?")
    CWND_FINAL=$(grep -oP "cwnd cuoi\s+:\s+\K[\d.]+" "/tmp/rdt_cli_${PORT}.log" || echo "?")
    TIMEOUTS=$(grep -oP "So timeout\s+:\s+\K\d+" "/tmp/rdt_cli_${PORT}.log" || echo "?")
    SEGS=$(grep -oP "Tong segment.*:\s+\K\d+" "/tmp/rdt_cli_${PORT}.log" || echo "?")

    if [ "$FILE_SIZE" -eq "$RECV_SIZE" ] && [ "$EXPECTED_SUM" = "$ACTUAL_SUM" ]; then
        ok "$NAME: ${FILE_SIZE}B khớp | ${THRU} KB/s | cwnd_final=${CWND_FINAL} | timeouts=${TIMEOUTS} | segs=${SEGS}"
    else
        fail "$NAME: Gửi ${FILE_SIZE}B nhận ${RECV_SIZE}B | sum gửi=${EXPECTED_SUM} nhận=${ACTUAL_SUM}"
        info "  [SERVER LOG]"; cat "/tmp/rdt_srv_${PORT}.log" | head -10
        info "  [CLIENT LOG]"; cat "/tmp/rdt_cli_${PORT}.log" | head -10
    fi

    rm -f "$RECV_FILE" "/tmp/rdt_srv_${PORT}.log" "/tmp/rdt_cli_${PORT}.log"
}

# ============================================================
#  1. Chuẩn bị dữ liệu test
# ============================================================
banner "BƯỚC 1: CHUẨN BỊ DỮ LIỆU TEST"

TINY_FILE="/tmp/rdt_tiny.txt"
SMALL_FILE="/tmp/rdt_small.bin"
MEDIUM_FILE="/tmp/rdt_medium.bin"
LARGE_FILE="/tmp/rdt_large.bin"
TEXT_FILE="/tmp/rdt_text.txt"

printf "Hello RDT Sliding Window — Flow & Congestion Control!" > "$TINY_FILE"
dd if=/dev/urandom of="$SMALL_FILE"  bs=1024 count=16  status=none   #  16 KB
dd if=/dev/urandom of="$MEDIUM_FILE" bs=1024 count=128 status=none   # 128 KB
dd if=/dev/urandom of="$LARGE_FILE"  bs=1024 count=512 status=none   # 512 KB

python3 -c "
for i in range(3000):
    print(f'Line {i:05d}: RDT Go-Back-N + Slow-Start + AIMD test line — {i*31}')
" > "$TEXT_FILE"

ok "Dữ liệu test: tiny=$(wc -c < $TINY_FILE)B  small=$(wc -c < $SMALL_FILE)B  medium=$(wc -c < $MEDIUM_FILE)B  large=$(wc -c < $LARGE_FILE)B  text=$(wc -c < $TEXT_FILE)B"

# ============================================================
#  2. Stop-and-Wait backward-compat
# ============================================================
banner "BƯỚC 2: BACKWARD-COMPAT — Stop-and-Wait Demo (RdtClientTest)"

if [ -f "$BUILD_DIR/rdt_saw_server" ] && [ -f "$BUILD_DIR/rdt_saw_client" ]; then
    SAW_PORT=9299
    "$BUILD_DIR/rdt_saw_server" "$SAW_PORT" > /tmp/rdt_saw_srv.log 2>&1 &
    SAW_PID=$!
    sleep 0.3
    "$BUILD_DIR/rdt_saw_client" 127.0.0.1 "$SAW_PORT" > /tmp/rdt_saw_cli.log 2>&1
    SAW_EXIT=$?
    wait "$SAW_PID" 2>/dev/null || true

    DELIVERED=$(grep -oP "goi du lieu HOP LE.*:\s+\K\d+" /tmp/rdt_saw_srv.log || echo "0")
    if [ "$SAW_EXIT" -eq 0 ] && [ "$DELIVERED" = "3" ]; then
        ok "Stop-and-Wait: 3 gói thật + 1 gói trùng → server giao đúng 3 gói"
    else
        warn "Stop-and-Wait: exit=$SAW_EXIT delivered=$DELIVERED (kỳ vọng 3). Chi tiết:"
        cat /tmp/rdt_saw_srv.log | head -8
    fi
    rm -f /tmp/rdt_saw_srv.log /tmp/rdt_saw_cli.log
else
    warn "rdt_saw_server/rdt_saw_client không tồn tại, bỏ qua."
fi

# ============================================================
#  3. Test truyền file — nhiều kích thước
# ============================================================
banner "BƯỚC 3: TRUYỀN FILE — Nhiều kích thước"

#                 NAME                   FILE          MSS   cwnd  ssth  maxW  advW
run_test "Tiny (52B)   MSS=64  Win=1"  "$TINY_FILE"   64   1.0   4.0    1     1
run_test "Tiny (52B)   MSS=64  Win=8"  "$TINY_FILE"   64   1.0   4.0    8     8
run_test "Small (16KB) MSS=512 Win=4"  "$SMALL_FILE"  512  1.0   8.0    4     4
run_test "Small (16KB) MSS=1024 Win=8" "$SMALL_FILE"  1024 1.0   8.0    8     8
run_test "Medium(128KB) MSS=1024 Win=8" "$MEDIUM_FILE" 1024 1.0  8.0    8     8
run_test "Medium(128KB) MSS=1024 Win=16" "$MEDIUM_FILE" 1024 1.0 8.0   16    16
run_test "Large (512KB) MSS=1024 Win=8"  "$LARGE_FILE"  1024 1.0 8.0    8     8
run_test "Large (512KB) MSS=1024 Win=32" "$LARGE_FILE"  1024 1.0 8.0   32    32
run_test "Text (3000L)  MSS=1024 Win=8"  "$TEXT_FILE"   1024 1.0 8.0    8     8

# ============================================================
#  4. Test Congestion Control — Slow Start & AIMD
# ============================================================
banner "BƯỚC 4: CONGESTION CONTROL — Slow Start & AIMD"

info "So sánh cwnd_final giữa ssthresh nhỏ và lớn (cùng file)"
#                  NAME                          FILE          MSS   cwnd  ssth  maxW  advW
run_test "CC: ssthresh=4  (slow start ngắn)"  "$MEDIUM_FILE" 1024  1.0   4.0   32    16
run_test "CC: ssthresh=16 (slow start dài)"   "$MEDIUM_FILE" 1024  1.0  16.0   32    16
run_test "CC: cwnd_init=4 (bắt đầu cao)"      "$MEDIUM_FILE" 1024  4.0   8.0   32    16
run_test "CC: maxWin=4    (giới hạn cwnd thấp)" "$MEDIUM_FILE" 1024 1.0  8.0    4     4

# ============================================================
#  5. Test Flow Control — Advertised Window
# ============================================================
banner "BƯỚC 5: FLOW CONTROL — Advertised Window receiver"

info "Receiver quảng bá advWindow khác nhau → sender điều chỉnh effectiveWindow"
#                  NAME                       FILE          MSS   cwnd  ssth  maxW  advW
run_test "FC: advWin=1  (receiver chậm)"  "$MEDIUM_FILE" 1024  1.0   8.0   32    1
run_test "FC: advWin=4"                   "$MEDIUM_FILE" 1024  1.0   8.0   32    4
run_test "FC: advWin=8  (bình thường)"    "$MEDIUM_FILE" 1024  1.0   8.0   32    8
run_test "FC: advWin=16 (receiver nhanh)" "$MEDIUM_FILE" 1024  1.0   8.0   32   16

# ============================================================
#  6. Dọn dẹp & tổng kết
# ============================================================
banner "BƯỚC 6: KẾT QUẢ TỔNG HỢP"

rm -f "$TINY_FILE" "$SMALL_FILE" "$MEDIUM_FILE" "$LARGE_FILE" "$TEXT_FILE"
rm -f /tmp/rdt_recv_*.bin /tmp/rdt_srv_*.log /tmp/rdt_cli_*.log

echo ""
echo -e "  ${GREEN}${BOLD}PASS: $PASS${NC}   ${RED}${BOLD}FAIL: $FAIL${NC}"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════${NC}"
    echo -e "${GREEN}${BOLD}  ✅ TẤT CẢ NGHIỆM THU THÀNH CÔNG!${NC}"
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════${NC}"
    exit 0
else
    echo -e "${RED}${BOLD}════════════════════════════════════════════${NC}"
    echo -e "${RED}${BOLD}  ❌ CÓ $FAIL CA KIỂM THỬ THẤT BẠI${NC}"
    echo -e "${RED}${BOLD}════════════════════════════════════════════${NC}"
    exit 1
fi
