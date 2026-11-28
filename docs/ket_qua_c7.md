# Kết quả thí nghiệm chính (C7): E1–E7

Tổng hợp số liệu của phiên đo chính ngày 28/11/2026, làm nền cho **chương 6** của báo cáo. Mọi số trong file này tính từ số liệu gốc ở `results/raw/c7_*/`; các bảng được sinh bằng script từ `results/summary/c7_*.csv`, không chép tay.

## Kết luận chính

1. **Ghim thread là yếu tố ảnh hưởng lớn nhất** (E7). Ở 6 thread, ghim mỗi thread vào một lõi vật lý tăng speedup thêm 54–64%: 1D từ 2,18 lên **3,37**, tile từ 2,19 lên **3,60**. Khi không ghim, Windows có thể xếp hai thread lên hai luồng SMT của cùng một lõi.
2. **Speedup nhân ma trận tốt nhất đạt khoảng 3,1 khi không ghim (12 thread) và 3,6 khi ghim (6 thread)**, so với bản tuần tự tốt nhất `mm_block_seq` b = 48 (E5, E7). Efficiency chỉ khoảng 0,6 ở 6 thread, có hai lý do:
   - bản song song chạy 1 thread đã chậm hơn bản tuần tự: 1D 0,64, tile 0,70, Cannon 0,90 (mục 6.1);
   - không ghim thread thì thread bị xếp chồng lên cùng lõi.
3. **Tích vô hướng bị giới hạn bởi băng thông RAM** (E1). Ở n = 10⁸, mọi biến thể song song dừng ở khoảng **29–30 GB/s**: bản `plain` tăng tốc tối đa 1,96 lần, bản `simd` chỉ 1,17 lần (vì bản tuần tự `simd` đã dùng 24,8 GB/s). Ở n nhỏ, bản song song chậm hơn bản tuần tự.
4. **Locality quyết định hằng số** (E2, E3). Thứ tự `ikj` nhanh nhất ở mọi n; `kji` chậm hơn **45 lần** ở n = 1500. Chia block với **b = 48** nhanh nhất (25,4–29,4 GFLOP/s), nhanh hơn không chia block 3,5 lần ở n = 2000. n = 1024 (lũy thừa của 2) chậm hơn n = 1000 tới 2,55 lần do conflict miss.
5. **Vector hóa AVX2 nhanh hơn 2,65–2,98 lần** ở 1 thread (E4).
6. **Lịch và barrier ảnh hưởng nhỏ** (E6, E7). `dynamic` nhanh hơn `static` ở 6 thread nhưng chênh lệch nằm trong nhiễu; Cannon có hay không có barrier giữa các bước cho thời gian gần như nhau.

## 1. Điều kiện đo

| Mục | Giá trị |
|---|---|
| Người đo, thời điểm | Cường, 28/11/2026, 15:04–15:18 (823 giây) |
| Máy | Intel Core i7-9850H, 6 lõi / 12 luồng, L1d 32 KiB, L2 256 KiB mỗi lõi, L3 12 MiB; RAM 15,7 GB |
| Hệ điều hành, compiler | Windows 11 (MSYS2 UCRT64), GCC 15.2.0, OpenMP (`_OPENMP` = 201511) |
| Cờ biên dịch | `-std=c++17 -O3 -march=native -Wall -Wextra -fopenmp` |
| Commit | `d93797c`, không có thay đổi chưa commit |
| Nguồn | **Cắm sạc, Power mode = Best performance**, kiểm tra cả lúc bắt đầu và lúc kết thúc (`meta.txt`) |
| Cấu hình | 7 file `scripts/experiments/c7_e*.conf`: 214 cấu hình × 5 lần = **1070 lần chạy**, chạy trong **một lệnh**, thứ tự xáo trộn chung (seed 2026) |
| Kiểm tra đúng đắn | Mỗi cấu hình qua `--verify` trước khi đo; `make test` qua 8/8 file test |
| Kết quả | 1070/1070 lần chạy thành công |

**Thống kê:** mỗi cấu hình lấy **median** `t_compute` của 5 lần chạy; độ phân tán là IQR (Q3 − Q1, nội suy tuyến tính của `numpy.percentile`). Speedup S = T1 / Tp, efficiency E = S / p, Karp–Flatt e = (1/S − 1/p) / (1 − 1/p). **T1** là bản tuần tự nhanh nhất trong **cùng thí nghiệm**, cùng n, cùng mức vector hóa (`scripts/figures.py`). Ký hiệu ⚠ trong các bảng: IQR lớn hơn 20% median.

| Thí nghiệm | Cấu hình | Lần chạy | IQR > 20% median | Cột `check` |
|---|---:|---:|---:|---|
| E1 tích vô hướng | 48 | 240 | 6 | 30 giống hệt; 18 lệch vài chữ số cuối (`~`), bình thường với reduction số thực |
| E2 thứ tự vòng lặp | 18 | 90 | 0 | giống hệt |
| E3 block size | 62 | 310 | 0 | giống hệt |
| E4 vector hóa | 18 | 90 | 1 | giống hệt |
| E5 phân hoạch | 46 | 230 | 9 | giống hệt |
| E6 lịch | 9 | 45 | 1 | giống hệt |
| E7 ghim thread, barrier | 13 | 65 | 2 | giống hệt |

Biểu đồ: `results/figures/c7_*/` (sinh bằng `python scripts/plot_results.py results/raw/c7_...`).

## 2. E1: tích vô hướng

`scripts/experiments/c7_e1_dot.conf`. Biểu đồ: `results/figures/c7_e1_dot/scaling_n*.png`.

**Bản tuần tự.** Băng thông = 16n byte / `t_compute` (đọc hai vector double).

| Biến thể | n | Dữ liệu (2 vector) | t_compute | GFLOP/s | Băng thông |
|---|---:|---:|---:|---:|---:|
| plain | 8192 | 128 KiB | 8,0 µs | 2,05 | 16,4 GB/s |
| plain | 262144 | 4 MiB | 257,2 µs | 2,04 | 16,3 GB/s |
| plain | 4194304 | 64 MiB | 4,56 ms | 1,84 | 14,7 GB/s |
| plain | 100000000 | 1,6 GB | 104,1 ms | 1,92 | 15,4 GB/s |
| simd | 8192 | 128 KiB | 2,2 µs | 7,45 | 59,6 GB/s |
| simd | 262144 | 4 MiB | 69,5 µs | 7,54 | 60,3 GB/s |
| simd | 4194304 | 64 MiB | 2,70 ms | 3,11 | 24,9 GB/s |
| simd | 100000000 | 1,6 GB | 64,57 ms | 3,10 | 24,8 GB/s |

**Speedup của `dot_omp`** (T1 = `dot_seq` cùng biến thể, cùng n). In đậm: tốt nhất của mỗi dòng.

| Biến thể | n | 1 thread | 2 thread | 4 thread | 6 thread | 12 thread |
|---|---:|---:|---:|---:|---:|---:|
| plain | 8192 | **0,43** | 0,31 | 0,21 | 0,12 | 0,07 |
| plain | 262144 | 0,99 | 1,28 | 1,24 | **1,35** | 1,17 |
| plain | 4194304 | 1,00 | 1,70 | 1,81 | **1,82** | 1,73 |
| plain | 100000000 | 0,99 | 1,77 | 1,91 | **1,96** | 1,91 |
| simd | 8192 | **0,26** | 0,12 | 0,07 | 0,05 | 0,02 |
| simd | 262144 | **0,80** | 0,51 | 0,67 | 0,42 | 0,40 |
| simd | 4194304 | 0,98 | 1,11 | **1,16** | 1,06 | 0,99 |
| simd | 100000000 | 1,01 | 1,12 | **1,17** | 1,10 | 1,02 |

**Nhận xét:**
- **Vector hóa ở 1 thread:** `simd` nhanh hơn `plain` 3,6–3,7 lần khi dữ liệu nằm trong cache (n ≤ 262144), nhưng chỉ 1,6 lần khi dữ liệu nằm ở RAM. Khi đó cả hai bị giới hạn bởi tốc độ đọc bộ nhớ (tài liệu lý thuyết, mục 7.3: không có `omp simd`, GCC phải cộng lần lượt).
- **Giới hạn băng thông:** ở n = 10⁸, bản song song nhanh nhất đạt khoảng **30,2 GB/s** (`plain`, 6 thread) và **29,0 GB/s** (`simd`, 4 thread); ở n = 4194304 khoảng 27–29 GB/s. Thêm thread không tăng thêm, vì mỗi phép nhân-cộng cần đọc 16 byte mà RAM không cấp nhanh hơn. Bản `simd` tuần tự đã dùng 24,8 GB/s, nên chỉ còn rất ít chỗ để song song hóa (tối đa 1,17 lần). Đây là ví dụ rõ cho mục 4.7 tài liệu lý thuyết: bài toán có cường độ tính toán thấp (2 flop / 16 byte) bị giới hạn bởi băng thông, không phải số lõi.
- **n nhỏ (8192, 128 KiB):** bản tuần tự chỉ mất 2–8 µs, ít hơn chi phí tạo và đồng bộ đội thread (hàng chục đến hàng trăm µs), nên song song hóa làm chậm đi; càng nhiều thread càng chậm.
- **n = 262144 (4 MiB, vừa L3):** `plain` tăng tốc 1,35 lần; `simd` thì chậm hơn tuần tự vì phép tính chỉ mất 69 µs, cùng cỡ chi phí fork-join.
- **12 thread không tốt hơn 6 thread** ở mọi n: băng thông RAM dùng chung cho cả 6 lõi.

## 3. E2: thứ tự vòng lặp

`scripts/experiments/c7_e2_loop.conf`. Phân tích và dự đoán: `docs/phan_tich_thu_tu_vong_lap.md` (V8, Vũ). Biểu đồ: `results/figures/c7_e2_loop/variants_mm_seq.png`.

Median `t_compute`, `mm_seq`, 1 thread:

| n | ijk | ikj | jik | jki | kij | kji | Chậm nhất / ikj |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 100 | 658,9 µs | **86,1 µs** | 667,9 µs | 334,2 µs | 114,8 µs | 527,8 µs | 7,8 lần |
| 600 | 199,1 ms | **26,78 ms** | 200,7 ms | 139,0 ms | 44,75 ms | 236,8 ms | 8,8 lần |
| 1500 | 6,81 s | **901,6 ms** | 6,22 s | 21,12 s | 1,64 s | 40,84 s | 45,3 lần |

Thứ tự nhanh → chậm:
- n = 100: ikj < kij < jki < kji < ijk < jik
- n = 600: ikj < kij < jki < ijk < jik < kji
- n = 1500: ikj < kij < jik < ijk < jki < kji

**Nhận xét:**
- Kết quả trùng với lần đo 22/11 (`results/raw/e2_loop/`): tỉ lệ thời gian giữa hai lần từ 0,95 đến 1,11. Mọi nhận xét trong mục "Kết quả E2" của `docs/phan_tich_thu_tu_vong_lap.md` vẫn đúng.
- `ikj` (mặc định của `mm_seq`) nhanh nhất ở mọi n: 23,2 / 16,1 / 7,5 GFLOP/s.
- n = 1500 (B vượt L3) khớp hoàn toàn thứ tự dự đoán theo mô hình số miss. Tỉ lệ kji / ikj = 45 lần, vượt tỉ lệ số miss 8 lần vì truy cập theo cột còn mất tiền nạp và chịu miss TLB.

## 4. E3: block size

`scripts/experiments/c7_e3_block.conf`. Dự đoán: `scripts/experiments/e3_block.conf` (D3, Đức). Biểu đồ: `results/figures/c7_e3_block/block_sweep_mm_block_seq_simd.png`.

GFLOP/s của `mm_block_seq` (kernel D4, 1 thread). In đậm: b nhanh nhất của mỗi n.

| b | n = 600 | n = 1000 | n = 2000 |
|---:|---:|---:|---:|
| 8 | 12,2 | 11,4 | 10,4 |
| 16 | 20,3 | 18,6 | 17,4 |
| 24 | 25,1 | 21,6 | 21,0 |
| 32 | 27,6 | 24,8 | 23,3 |
| 40 | 27,2 | 23,4 | 23,2 |
| 48 | 29,4 | **25,9** | **25,4** |
| 56 | **29,5** | 25,3 | 24,5 |
| 64 | 23,7 | 20,9 | 20,5 |
| 72 | 22,3 | 20,7 | 20,0 |
| 80 | 22,2 | 20,9 | 20,3 |
| 96 | 23,2 | 21,1 | 20,5 |
| 128 | 23,2 | 21,5 | 21,1 |
| 160 | 20,3 | 17,8 | 17,0 |
| 192 | 15,2 | 14,8 | 14,2 |
| 256 | 16,2 | 13,8 | 12,8 |
| 384 | 15,4 | 13,8 | 13,9 |
| 512 | 16,2 | 14,7 | 14,6 |
| b = n (không chia block) | 17,9 | 14,4 | 7,3 |

| n | b tốt nhất | Thời gian (b tốt nhất) | Thời gian (b = n) | Nhanh hơn |
|---:|---:|---:|---:|---:|
| 600 | 56 (b = 48 kém 0,1 GFLOP/s) | 14,66 ms | 24,18 ms | 1,65 lần |
| 1000 | 48 | 77,22 ms | 139,2 ms | 1,80 lần |
| 2000 | 48 | 629,4 ms | 2,18 s | **3,46 lần** |

**Nhận xét:**
- **b = 48–56 nhanh nhất, b = 64 đã chậm hơn b = 48 khoảng 19%** ở cả ba n. Khớp dự đoán của Đức: block B (8b² + 16b byte) vừa L1 32 KiB khi b ≤ 63.
- Bậc thang thứ hai ở b = 72–128 (B ra khỏi L1 nhưng còn trong L2), rồi giảm mạnh từ b = 160–192.
- Lợi ích của chia block lớn nhất khi B vượt L3 (n = 2000: 3,46 lần); ở n = 600 cả ba ma trận vừa L3 nên chỉ 1,65 lần.
- **So với lần đo 22/11** (`docs/ket_qua_e3_block_size.md`): b tốt nhất giữ nguyên (48), nhưng GFLOP/s ở b = 48 cao hơn nhiều (n = 600: 7,9 → 29,4; n = 1000: 16,2 → 25,9; n = 2000: 18,3 → 25,4) và không còn cấu hình nhiễu (0/62 so với 8/62). Phiên 22/11 không kiểm soát chế độ nguồn; phiên C7 cắm sạc và đặt Best performance. Chương 6 dùng số của C7.

**Conflict miss: n = 1024 so với n = 1000** (n = 1024 chỉ nhiều hơn 7,4% phép tính):

| b | n = 1000 | n = 1024 | 1024 chậm hơn |
|---:|---:|---:|---:|
| 16 | 107,4 ms | 186,4 ms | 1,74 lần |
| 32 | 80,77 ms | 139,7 ms | 1,73 lần |
| 48 | 77,22 ms | 196,9 ms | **2,55 lần** |
| 64 | 95,63 ms | 196,9 ms | 2,06 lần |
| 96 | 94,99 ms | 186,5 ms | 1,96 lần |
| 128 | 93,08 ms | 172,5 ms | 1,85 lần |
| 256 | 144,8 ms | 160,4 ms | 1,11 lần |

Các hàng của ma trận n = 1024 cách nhau 8 KiB (bội của 4 KiB), nên rơi vào cùng các set của L1 và L1 chỉ giữ được vài hàng của block B (`e3_block.conf`, tài liệu lý thuyết mục 4.3). Hiệu ứng mạnh nhất ở chính các b tốt nhất (48, 64), nơi chia block dựa vào việc giữ B trong L1. Với n = 1024, b tốt nhất chỉ còn b = 32 (15,4 GFLOP/s).

## 5. E4: vector hóa

`scripts/experiments/c7_e4_vec.conf`: cùng kernel D4, `--variant simd` (AVX2) hoặc `novec` (`#pragma GCC novector`), b = 48. Biểu đồ: `results/figures/c7_e4_vec/`.

| n | 1 thread simd | 1 thread novec | simd nhanh hơn | 6 thread simd (tile) | 6 thread novec (tile) | simd nhanh hơn |
|---:|---:|---:|---:|---:|---:|---:|
| 600 | 14,82 ms (29,1 GFLOP/s) | 44,11 ms (9,8) | 2,98 lần | 8,48 ms | 14,85 ms | 1,75 lần |
| 1000 | 77,34 ms (25,9 GFLOP/s) | 213,2 ms (9,4) | 2,76 lần | 27,16 ms | 82,63 ms | 3,04 lần |
| 2000 | 642,4 ms (24,9 GFLOP/s) | 1,70 s (9,4) | 2,65 lần | 256,5 ms | 714,5 ms | 2,78 lần |

**Nhận xét:**
- AVX2 xử lý 4 double mỗi lệnh FMA; đo được nhanh hơn **2,65–2,98 lần** ở 1 thread. Không đạt 4 lần vì phần tải/ghi bộ nhớ và chi phí vòng lặp không được tăng tốc tương ứng.
- Ở 6 thread, lợi ích vẫn khoảng 2,8–3,0 lần ở n = 1000, 2000: vector hóa và song song hóa cộng dồn được. Riêng n = 600 chỉ 1,75 lần, vì ở 6 thread phép tính chỉ còn 8,5 ms và chi phí chia việc chiếm phần lớn.
- Speedup 6 thread so với chính bản tile 1 thread (S_self): simd 2,6–4,0, novec 3,2–4,0. Cả hai biến thể mở rộng ở mức tương tự, nên vector hóa không làm giảm khả năng song song hóa.

## 6. E5: cách phân hoạch (1D, tile, Cannon)

`scripts/experiments/c7_e5_part.conf`: b = 48, lịch static, **không ghim thread**. Cannon cần số thread chính phương (9 thread vượt 6 lõi vật lý nên có dùng SMT). Biểu đồ: `results/figures/c7_e5_part/scaling_n*.png`.

Speedup so với `mm_block_seq` b = 48 cùng phiên:

**n = 600** (T1 = 14,88 ms, 29,0 GFLOP/s)

| Phân hoạch | 1 | 2 | 4 | 6 | 9 | 12 |
|---|---:|---:|---:|---:|---:|---:|
| 1D | 0,60 | 1,18 | 2,30 | 2,08 ⚠ | — | 2,90 |
| tile | 0,68 | 1,28 | 2,48 ⚠ | 1,77 | — | 2,57 |
| Cannon | 0,86 | — | 2,89 ⚠ | — | 2,76 | — |

**n = 1000** (T1 = 75,80 ms, 26,4 GFLOP/s)

| Phân hoạch | 1 | 2 | 4 | 6 | 9 | 12 |
|---|---:|---:|---:|---:|---:|---:|
| 1D | 0,63 | 1,28 | 1,83 ⚠ | 2,59 ⚠ | — | **3,29** |
| tile | 0,69 | 1,40 | 2,60 ⚠ | 2,72 ⚠ | — | 2,79 ⚠ |
| Cannon | 0,90 | — | **3,39** | — | 2,39 | — |

**n = 2000** (T1 = 630,6 ms, 25,4 GFLOP/s; kích thước lớn)

| Phân hoạch | 1 | 2 | 4 | 6 | 9 | 12 |
|---|---:|---:|---:|---:|---:|---:|
| 1D | 0,64 | 1,24 | 2,05 | 2,08 | — | 3,03 |
| tile | 0,70 | 1,30 | 1,85 | 2,45 ⚠ | — | **3,12** |
| Cannon | 0,90 | — | 2,81 | — | 2,66 | — |

### 6.1. Bản song song chạy 1 thread chậm hơn bản tuần tự

Ở 1 thread, 1D chỉ đạt 0,60–0,64, tile 0,68–0,70, Cannon 0,86–0,90 lần tốc độ `mm_block_seq`. Cả ba gọi cùng kernel nên kết quả giống từng bit. Review D5 (`docs/ket_qua_tile_shape.md`, mục 4) đã chỉ ra:
- nguyên nhân không phải hình dạng tile, không phải ghim thread, không riêng vector hóa;
- nhiều khả năng do mã kernel bị biên dịch kém hơn khi nằm trong vùng `omp parallel`.

Cannon mất ít nhất (khoảng 10%), có thể vì mỗi thread gọi kernel trên các khối lớn n/q × n/q nên phần mã quanh lời gọi ít hơn; **chưa kiểm chứng**. Đây là nguồn mất mát lớn nhất trước khi tính đến số thread, và là hướng cải tiến đầu tiên (chương 7). Nhóm quyết định **không sửa mã sau khi đóng băng**, vì sửa thì phải đo lại toàn bộ E4–E7.

### 6.2. So sánh các cách phân hoạch

- **Đến 4 thread, Cannon tốt nhất** (2,81–3,39), một phần nhờ chi phí 1 thread thấp (mục 6.1). Mỗi thread giữ cố định một khối C và làm việc trên khối lớn, nên locality tốt.
- **Cannon 9 thread kém hơn 4 thread** (2,39–2,76): 9 thread trên 6 lõi vật lý buộc dùng SMT, và lưới 3 × 3 không chia đều cho 6 lõi.
- **1D và tile gần như ngang nhau** ở mọi n; khác biệt thường nằm trong nhiễu. Ở 12 thread cả hai đạt khoảng 2,6–3,3.
- **6 thread không ghim thường không tốt hơn 4 thread nhiều** (n = 2000: 1D 2,05 → 2,08). Lý do nằm ở E7: không ghim thì nhiều thread bị xếp lên cùng lõi. 12 thread lấp đủ 12 CPU logic nên tình trạng này ít hơn, vì vậy không ghim thì 12 thread thường tốt hơn 6.
- Efficiency giảm từ khoảng 0,6–0,9 (1 thread) xuống khoảng 0,25 (12 thread). Karp–Flatt (`results/summary/c7_e5_part_speedup.csv`) với 1D và tile lớn nhất ở p = 2 (0,43–0,70) rồi nhìn chung giảm, còn 0,24–0,48 ở 6 và 12 thread. Cannon 0,06–0,14 ở 4 thread, tăng lên 0,28–0,35 ở 9 thread (có SMT). e lớn ở p nhỏ và giảm khi p tăng cho thấy phần mất mát chính là **một hệ số cố định** của bản song song (chậm sẵn 30–40% ở 1 thread, mục 6.1), không phải chi phí đồng bộ tăng theo số thread.

**b = 32 so với b = 48 ở n = 2000** (SMT chia đôi L1, nên block B chỉ vừa L1 khi b ≤ 44):

| Phân hoạch | Thread | b = 48 | b = 32 |
|---|---:|---:|---:|
| 1D | 6 | 303 ms (S = 2,08) | 304 ms (S = 2,08) |
| 1D | 12 | 208 ms (S = 3,03) | 196 ms (S = 3,22) |
| tile | 6 | 258 ms (S = 2,45) | 285 ms (S = 2,21) |
| tile | 12 | 202 ms (S = 3,12) | 205 ms (S = 3,07) |

b = 32 chỉ giúp 1D ở 12 thread (khoảng 6%); các trường hợp khác không tốt hơn. Giữ **b = 48** cho mọi bản song song.

## 7. E6: lập lịch static hay dynamic

`scripts/experiments/c7_e6_sched.conf`: n = 2000, b = 48, không ghim. Biểu đồ: `results/figures/c7_e6_sched/compare_n2000.png`.

| Phân hoạch | Thread | static | dynamic | dynamic so với static |
|---|---:|---:|---:|---|
| 1D | 6 | 294,8 ms (S = 2,15) | 277,5 ms (S = 2,28) | nhanh hơn 6% (trong nhiễu) |
| 1D | 12 | 212,9 ms (S = 2,98) | 231,5 ms (S = 2,74) | chậm hơn 8% (vượt IQR) |
| tile | 6 | 297,0 ms (S = 2,13) | 258,9 ms (S = 2,45) | nhanh hơn 15% (trong nhiễu) |
| tile | 12 | 207,2 ms (S = 3,06) | 203,1 ms (S = 3,12) | nhanh hơn 2% (trong nhiễu) |

"Vượt IQR": chênh lệch median lớn hơn IQR của cả hai cấu hình.

**Nhận xét:**
- Mọi khối việc có cùng khối lượng (trừ khối ở biên), nên về lý thuyết `static` là đủ (tài liệu lý thuyết, mục 8.4).
- Ở 6 thread không ghim, `dynamic` có xu hướng nhanh hơn: khi hai thread bị xếp chung một lõi và chạy chậm, `dynamic` để các thread nhanh lấy thêm việc. Tuy vậy chênh lệch nằm trong nhiễu.
- Ở 12 thread, `dynamic` của 1D chậm hơn 8% (vượt IQR): dải cao b = 48 hàng tạo 42 dải cho 12 thread; chi phí lấy việc và mất locality giữa các dải lớn hơn lợi ích cân bằng tải.
- Kết luận: lịch không phải yếu tố quyết định trên máy này; giữ `static` làm mặc định.

## 8. E7: ghim thread và barrier

`scripts/experiments/c7_e7_pin.conf`: n = 2000, b = 48, lịch static. Ghim bằng Win32 (`common/affinity`, Đức), mỗi thread một lõi vật lý trước rồi mới tới luồng SMT. Biểu đồ: `results/figures/c7_e7_pin/compare_n2000.png`.

| Phân hoạch | Thread | Không ghim | Ghim | Ghim nhanh hơn |
|---|---:|---:|---:|---|
| 1D | 4 | 372,5 ms (S = 1,71) | 280,7 ms (S = 2,27) | 33% (vượt IQR) |
| 1D | 6 | 292,6 ms (S = 2,18) | 189,4 ms (S = **3,37**) | **54%** (vượt IQR) |
| tile | 4 | 361,6 ms (S = 1,76) | 265,1 ms (S = 2,41) | 36% (trong nhiễu) |
| tile | 6 | 291,4 ms (S = 2,19) | 177,3 ms (S = **3,60**) | **64%** (vượt IQR) |
| Cannon barrier | 4 | 230,8 ms (S = 2,76) | 208,8 ms (S = 3,06) | 11% (trong nhiễu) |
| Cannon nobarrier | 4 | 232,2 ms (S = 2,75) | 204,5 ms (S = 3,12) | 14% (vượt IQR) |

**Nhận xét:**
- **Ghim thread tăng tốc rõ rệt nhất ở 6 thread** (54–64%), tức đúng số lõi vật lý. Bộ lập lịch của Windows không phân biệt lõi vật lý và luồng SMT, nên có thể đặt hai thread lên cùng một lõi, khi đó chúng chia nhau đơn vị FMA và L1, L2. Ghim mỗi thread một lõi vật lý loại bỏ việc này. `OMP_PROC_BIND`/`OMP_PLACES` không có tác dụng trên MSYS2 (tài liệu lý thuyết, mục 8.8), nên phải ghim bằng Win32.
- Speedup tốt nhất của đồ án: **tile 6 thread có ghim, S = 3,60** (efficiency 0,60). Cùng xu hướng với lần đo `tile_shape` ngày 23/11 (có ghim, 6 thread, static: S = 3,34–3,70 tùy bề rộng tile).
- Cannon được lợi ít hơn (11–14%), vì 4 thread trên 6 lõi ít bị xếp chồng hơn, và Cannon vốn đã có locality tốt.
- **Barrier không ảnh hưởng đáng kể:** Cannon có barrier 230,8 / 208,8 ms, không barrier 232,2 / 204,5 ms. Mỗi bước của Cannon tính một khối lớn (1000 × 1000 với n = 2000, q = 2), nên chi phí chờ nhau ở barrier rất nhỏ so với thời gian tính, và các thread có cùng khối lượng việc nên ít khi phải chờ.
- **Hệ quả cho E5:** E5 đo không ghim nên đường speedup ở 4–6 thread thấp hơn khả năng thật của máy. Kết luận về thứ hạng các cách phân hoạch vẫn giữ, nhưng con số speedup cao nhất nên trích từ E7.

## 9. Hạn chế

- **Một máy đo duy nhất** (laptop i7-9850H); kết quả về SMT, ghim thread và băng thông phụ thuộc phần cứng và Windows.
- **`t_io` bằng 0** ở mọi lần chạy: dữ liệu sinh theo seed (tính vào `t_init`), không đọc file. Chương trình vẫn đo `t_io` khi dùng `--input/--output` (kiểm tra bằng `data/sample/`).
- **5 lần mỗi cấu hình**: một số cấu hình ngắn (vài ms) có IQR trên 20% (đánh dấu ⚠); kết luận chỉ dựa trên chênh lệch vượt IQR hoặc lặp lại ở nhiều n.
- **E5 không ghim thread**, nên chưa có đường speedup 1–12 thread khi ghim; E7 chỉ có 4 và 6 thread ở n = 2000.
- **Nguyên nhân bản song song chạy 1 thread chậm hơn** (mục 6.1) mới được khoanh vùng, chưa xác định chính xác; cần đọc mã máy hoặc dùng bộ đếm phần cứng (VTune).
- Số liệu các phiên trước (E2, E3 ngày 22/11; `tile_shape` ngày 23/11) không kiểm soát chế độ nguồn; chỉ dùng để đối chiếu xu hướng, **không trộn** với số liệu C7.
