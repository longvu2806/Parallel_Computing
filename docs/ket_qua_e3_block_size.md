# Kết quả E3: khảo sát block size (C5)

Thí nghiệm **E3**: block size nào tốt nhất cho `mm_block_seq` (nhân tuần tự theo block, 1 thread) với kernel tối ưu D4 (`matmul/mm_kernel.hpp`, `--variant simd`)?

- Dự đoán và phân tích cache: phần chú thích của `scripts/experiments/e3_block.conf` (D3, Đức).
- Số liệu gốc: `results/raw/e3_block/mm_block_seq.csv`, kèm `meta.txt`.
- Bảng đầy đủ (min, max, Q1, Q3, IQR, độ lệch chuẩn): `python scripts/plot_results.py results/raw/e3_block`.

## 1. Điều kiện đo

| Mục | Giá trị |
|---|---|
| Người đo, ngày | Cường, 22/11/2026 |
| Máy | Intel Core i7-9850H, 6 lõi / 12 luồng, L1d 32 KiB, L2 256 KiB mỗi lõi, L3 12 MiB; RAM 15,7 GB |
| Hệ điều hành, compiler | Windows 11 (MSYS2 UCRT64), GCC 15.2.0 |
| Cờ biên dịch | `-std=c++17 -O3 -march=native -Wall -Wextra -fopenmp` |
| Commit | `bbea693` (có kernel D4), không có thay đổi chưa commit |
| Cấu hình | `scripts/experiments/e3_block.conf`: 62 cấu hình, 5 lần mỗi cấu hình, thứ tự xáo trộn (seed 2026) |
| Kiểm tra | Mọi cấu hình qua `--verify` trước khi đo; cột `check` giống nhau ở cả 5 lần chạy |
| Thời gian chạy | 310 lần, 266 giây |

Mọi số dưới đây tính từ **median** `t_compute` của 5 lần chạy. GFLOP/s = 2n³ / median `t_compute`.

## 2. GFLOP/s theo block size

In đậm: b tốt nhất của mỗi n. ⚠: IQR lớn hơn 20% median, số đo nhiễu, đọc thận trọng.

| b | n = 600 | n = 1000 | n = 2000 |
|---:|---:|---:|---:|
| 8 | 4,5 | 8,1 | 7,7 |
| 16 | 6,0 | 11,6 | 12,8 |
| 24 | 7,1 | 13,4 | 16,4 |
| 32 | 7,4 | 13,9 | 17,0 |
| 40 | **9,6** ⚠ | 14,0 ⚠ | 16,8 |
| 48 | 7,9 | **16,2** | **18,3** |
| 56 | 8,0 | 13,9 | 16,6 |
| 64 | 6,5 | 12,5 ⚠ | 15,4 |
| 72 | 6,3 | 12,1 | 13,7 |
| 80 | 6,4 | 12,7 | 14,9 |
| 96 | 6,8 | 13,4 ⚠ | 15,5 |
| 128 | 7,3 ⚠ | 10,7 ⚠ | 15,4 |
| 160 | 5,4 | 9,3 | 12,7 |
| 192 | 5,0 | 9,1 | 10,4 |
| 256 | 4,4 | 5,8 | 8,7 |
| 384 | 4,7 | 7,7 | 9,6 |
| 512 | 4,5 | 9,5 | 9,8 |
| b = n (không chia block) | 5,1 | 9,1 | 4,8 |

Tăng tốc của b tốt nhất so với không chia block:

| n | b tốt nhất | Thời gian (b tốt nhất) | Thời gian (b = n) | Nhanh hơn |
|---:|---:|---:|---:|---:|
| 600 | 40 | 44,9 ms | 85,2 ms | 1,9 lần |
| 1000 | 48 | 123 ms | 220 ms | 1,8 lần |
| 2000 | 48 | 0,876 s | 3,32 s | **3,8 lần** |

## 3. Đối chiếu với dự đoán

| Dự đoán (`e3_block.conf`) | Đo được | Khớp? |
|---|---|---|
| b ≤ 16 chậm: vòng j quá ngắn, tốn chi phí vòng lặp | b = 8 chỉ đạt 4,5–8,1 GFLOP/s, chậm nhất hoặc gần chậm nhất ở mọi n | Khớp |
| Nhanh nhất ở b khoảng 48–64, khi block B (8b² + 16b byte) vừa L1 32 KiB, tức b ≤ 63; b = 64 sát biên L1 nên có thể đã chậm lại | Nhanh nhất ở **b = 48** (n = 1000, 2000); vùng 24–56 luôn gần tốt nhất; **b = 64 đạt GFLOP/s thấp hơn b = 48 từ 16% đến 23%** | Khớp |
| Bậc thang thứ hai ở b khoảng 72–160: B ra khỏi L1 nhưng còn trong L2 | b = 72–128 đạt 13,7–15,5 GFLOP/s ở n = 2000, thấp hơn đỉnh nhưng ổn định | Khớp |
| b ≥ 192 chậm tiếp: B (8b²) ra khỏi L2 khi b > 181; ba block (24b²) ra khỏi L2 khi b > 104 | Rơi rõ từ b = 160 (12,7) và b = 192 (10,4) ở n = 2000 | Khớp |
| Lợi ích của chia block lớn nhất khi B vượt L3 (n = 2000) | Nhanh hơn không chia block 3,8 lần ở n = 2000, chỉ 1,8–1,9 lần ở n = 600, 1000 | Khớp |

**So với máy Đức** (i5-12450H, L1d 48 KiB, biên L1 ở b khoảng 77): máy Đức tốt nhất ở b = 64, máy đo tốt nhất ở b = 48. Biên dịch chuyển đúng theo dung lượng L1 của mỗi máy, phù hợp với giải thích "block B phải vừa L1".

## 4. Conflict miss khi n là lũy thừa của 2 (n = 1024 so với n = 1000)

n = 1024 chỉ nhiều hơn n = 1000 khoảng 7% phép tính (1,074 lần). Chậm hơn nhiều so với tỉ lệ đó là do xung đột set trong L1: các hàng cách nhau 8 KiB, là bội của 4 KiB, nên rơi vào cùng các set (`e3_block.conf`, phần "Tranh n la luy thua cua 2").

| b | n = 1000 | n = 1024 | 1024 chậm hơn | GFLOP/s 1000 | GFLOP/s 1024 |
|---:|---:|---:|---:|---:|---:|
| 16 | 173 ms | 281 ms | 1,63 lần | 11,6 | 7,6 |
| 32 | 144 ms | 229 ms | 1,59 lần | 13,9 | 9,4 |
| 48 | 123 ms | 292 ms | **2,37 lần** | 16,2 | 7,4 |
| 64 | 161 ms | 362 ms | **2,25 lần** | 12,5 | 5,9 |
| 96 | 149 ms | 292 ms | 1,96 lần | 13,4 | 7,4 |
| 128 | 188 ms | 262 ms | 1,39 lần | 10,7 | 8,2 |
| 256 | 344 ms | 246 ms | 0,72 lần | 5,8 | 8,7 |
| b = n | 220 ms | 282 ms | 1,29 lần | 9,1 | 7,6 |

- Ở các b tốt nhất (48, 64), n = 1024 chậm hơn **2,2–2,4 lần**, lớn hơn mức khoảng 1,8 lần đo được trên máy Đức. L1 của máy đo nhỏ hơn (32 KiB so với 48 KiB), nên mất vài set ảnh hưởng nặng hơn.
- Ở n = 1024, b tốt nhất chỉ còn 9,4 GFLOP/s (b = 32), bằng khoảng một nửa đỉnh của n = 1000.
- b = 256 là ngoại lệ: n = 1000 chậm hơn n = 1024. Ở n = 1000, b = 256 cũng thấp bất thường so với b = 192 và b = 384. **Chưa giải thích được.** Có thể kiểm tra lại bằng cách đo riêng cấu hình này với nhiều lần chạy hơn.

## 5. Độ tin cậy

- 8/62 cấu hình có IQR trên 20% median, tập trung ở n = 600 và n = 1000. Ở các cỡ này mỗi lần chạy chỉ mất 40–200 ms, nên nhiễu của laptop (tần số turbo, tiến trình nền) chiếm tỉ lệ lớn. E3 nhiễu hơn E2 (E2 chỉ có 1/18 cấu hình IQR trên 20%).
- Kết luận về b tốt nhất vẫn vững: b = 48 dẫn đầu ở cả n = 1000 và n = 2000 với khoảng cách rõ, và vùng 24–56 luôn nhanh hơn các vùng còn lại ở mọi n. Riêng b = 40 ở n = 600 (IQR 34%) không đủ tin cậy để coi là tốt nhất.
- Nếu cần đường cong mượt hơn cho báo cáo: đo lại riêng n = 600, 1000 với `-r 10`, đóng hết ứng dụng nền và thêm `-p 1` (nghỉ 1 giây giữa các lần chạy).

## 6. Đề xuất block size cho các bản song song (họp K4)

- **b = 48** làm mặc định cho 1D, tile, Cannon. Đây là b tốt nhất ở các cỡ lớn trên máy đo.
- Ở 12 thread, hai thread SMT dùng chung L1 của một lõi. Theo `e3_block.conf`, khi chia đôi L1 thì block B chỉ vừa nếu b ≤ 44. Vì vậy E5 nên đo thêm **b = 32** cho cấu hình 12 thread (E3 cho thấy b = 32 chỉ chậm hơn b = 48 khoảng 7% khi chạy 1 thread).
- Tránh n là lũy thừa của 2 trong mọi thí nghiệm chính (mục 4).
