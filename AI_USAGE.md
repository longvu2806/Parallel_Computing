# Nhật ký sử dụng AI

## Lịch sử

### 1

- Thời gian: 16/11/2026
- Người sử dụng: Cường
- Công cụ/mô hình: Claude Code (Claude Opus 5.5)
- Mục đích: Cài đặt việc C1 trong kế hoạch: timer đo theo pha (`common/timer.hpp`) kèm test (`tests/test_timer.cpp`).  Xác định yêu cầu từ kế hoạch và tài liệu lý thuyết (mục 9.2: dùng `steady_clock`, tách 3 pha init/compute/io), giao AI viết mã, rồi tự review và kiểm chứng.
- Prompt hoặc tóm tắt prompt:
  1. "đọc folder docs và triển khai phần của cường (triển khai theo từng giai đoạn không làm tất cả cùng lúc)"
  2. Gửi ảnh lỗi IntelliSense `std::this_thread` để AI tìm nguyên nhân (VS Code dùng nhầm MinGW 6.3)
- Kết quả: AI viết lớp `pc::PhaseTimer` dùng `std::chrono::steady_clock`, cộng dồn 3 pha init/compute/io, `total()` là wall clock từ đầu `main`, RAII `Scope` tự tắt pha kể cả khi có ngoại lệ, báo lỗi khi hai pha chồng nhau; và chương trình test 13 kiểm tra.
- Phần đã sử dụng: Toàn bộ `common/timer.hpp` và `tests/test_timer.cpp` do AI viết, sau khi đã đọc và hiểu từng hàm.
- Chỉnh sửa của nhóm:
- Kiểm chứng: Tự chạy `make` trong MSYS2 UCRT64 trên máy đo i7-9850H (GCC 15.2, `-O3 -march=native -Wall -Wextra -fopenmp`): build không cảnh báo, `build/test_timer.exe` qua 13/13 kiểm tra; đo được bước nhỏ nhất của `steady_clock` là 100 ns, trong khi `omp_get_wtick()` = 1 ms, khớp lý do chọn `steady_clock` trong tài liệu lý thuyết.
- Commit/issue liên quan: nhánh `cuong/timer`

## Tuyên bố

Nhóm chịu trách nhiệm về tính đúng đắn, bản quyền, số liệu thực nghiệm và mọi nội dung đã nộp.
