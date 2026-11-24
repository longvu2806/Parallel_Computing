# Kết quả thí nghiệm phụ tile_shape và review D5

Hai việc ngày 23/11/2026, phục vụ chốt cấu hình trước E5, E6:
1. **tile_shape**: tile của `mm_par_2d` (D5, Đức) nên rộng bao nhiêu: b×b, b×2b hay b×4b?
2. **Review D5** (Cường, theo mục 4.6 kế hoạch): phát hiện bản tile chạy 1 thread chậm hơn bản tuần tự.

- Câu hỏi, giả thuyết và quy tắc chốt: phần chú thích của `scripts/experiments/tile_shape.conf` (Đức).
- Số liệu gốc: `results/raw/tile_shape/mm_par_2d.csv`, `mm_block_seq.csv`, kèm `meta.txt`.
- Bảng đầy đủ: `python scripts/plot_results.py results/raw/tile_shape`.

## 1. Điều kiện đo

| Mục | Giá trị |
|---|---|
| Người đo, ngày | Cường, 23/11/2026 |
| Máy | Intel Core i7-9850H, 6 lõi / 12 luồng, L1d 32 KiB, L2 256 KiB mỗi lõi, L3 12 MiB; RAM 15,7 GB |
| Hệ điều hành, compiler, cờ | Windows 11 (MSYS2 UCRT64), GCC 15.2.0, `-std=c++17 -O3 -march=native -Wall -Wextra -fopenmp` |
| Commit | `01925ff` (có D5), không có thay đổi chưa commit |
| Cấu hình | `tile_shape.conf`: 54 cấu hình `mm_par_2d` (n = 600, 1000, 2000; 1, 6, 12 thread; static, dynamic; w1, w2, w4; b = 48; ghim thread) và 3 cấu hình mốc `mm_block_seq` (b = 48). 5 lần mỗi cấu hình, thứ tự xáo trộn |
| Kiểm tra | Mọi cấu hình qua `--verify`; `check` giống nhau ở cả 5 lần chạy; `make test` qua 8/8 file test |
| Thời gian chạy | 285 lần, 98 giây |

Mọi số tính từ **median** `t_compute` của 5 lần chạy.

## 2. Bề rộng tile

Thời gian (ms). In đậm: bề rộng nhanh nhất của mỗi dòng. ⚠: IQR lớn hơn 20% median. Cột cuối: w4 so với w1 có vượt độ phân tán (chênh lệch median lớn hơn IQR của cả hai) hay không.

| n | Thread | Lịch | w1 (b×b) | w2 (b×2b) | w4 (b×4b) | Nhanh nhất | w4 so với w1 |
|---:|---:|---|---:|---:|---:|---|---|
| 600 | 1 | dynamic | **26,3** ⚠ | 26,3 | 26,9 ⚠ | w1 | w4 chậm hơn 2% (trong nhiễu) |
| 600 | 1 | static | **23,0** ⚠ | 24,3 | 25,0 ⚠ | w1 | w4 chậm hơn 8% (trong nhiễu) |
| 600 | 6 | dynamic | 5,9 | 5,1 | **4,7** | w4 | w4 nhanh hơn 25% (**vượt IQR**) |
| 600 | 6 | static | 7,1 | **7,0** ⚠ | 7,4 ⚠ | w2 | w4 chậm hơn 5% (trong nhiễu) |
| 600 | 12 | dynamic | 7,0 ⚠ | 5,8 | **5,5** | w4 | w4 nhanh hơn 27% (trong nhiễu) |
| 600 | 12 | static | 5,8 ⚠ | **5,4** ⚠ | 6,6 ⚠ | w2 | w4 chậm hơn 13% (trong nhiễu) |
| 1000 | 1 | dynamic | 142,0 | **128,3** | 131,1 | w2 | w4 nhanh hơn 8% (trong nhiễu) |
| 1000 | 1 | static | 122,7 | 127,1 | **117,2** | w4 | w4 nhanh hơn 5% (trong nhiễu) |
| 1000 | 6 | dynamic | **24,7** ⚠ | 25,4 | 27,8 ⚠ | w1 | w4 chậm hơn 11% (trong nhiễu) |
| 1000 | 6 | static | **23,2** ⚠ | 28,8 | 28,4 ⚠ | w1 | w4 chậm hơn 18% (trong nhiễu) |
| 1000 | 12 | dynamic | **26,5** | 27,3 | 27,0 | w1 | w4 chậm hơn 2% (trong nhiễu) |
| 1000 | 12 | static | 34,9 ⚠ | **28,0** | 28,7 ⚠ | w2 | w4 nhanh hơn 22% (trong nhiễu) |
| 2000 | 1 | dynamic | 1020,1 | **1000,2** | 1024,0 | w2 | w4 chậm hơn 0% (trong nhiễu) |
| 2000 | 1 | static | 988,6 | 967,4 | **942,4** | w4 | w4 nhanh hơn 5% (trong nhiễu) |
| 2000 | 6 | dynamic | 199,2 | 198,3 | **186,9** | w4 | w4 nhanh hơn 7% (**vượt IQR**) |
| 2000 | 6 | static | 195,8 ⚠ | **176,6** | 182,8 | w2 | w4 nhanh hơn 7% (trong nhiễu) |
| 2000 | 12 | dynamic | 214,7 | 210,5 | **205,9** | w4 | w4 nhanh hơn 4% (trong nhiễu) |
| 2000 | 12 | static | 228,2 | 206,9 | **203,6** | w4 | w4 nhanh hơn 12% (trong nhiễu) |

**Kết luận:**
- Bề rộng nhanh nhất chia gần đều: w4 ở 7 dòng, w2 ở 6 dòng, w1 ở 5 dòng.
- w4 nhanh hơn w1 vượt độ phân tán chỉ ở **2/18** cấu hình. Ở các dòng còn lại chênh lệch nằm trong nhiễu, có dòng w1 còn nhanh hơn (n = 1000, 6 thread).
- Giả thuyết "tile rộng thì đỡ chạm trang nhớ mới" (`tile_shape.conf`) **không được xác nhận rõ** trên máy đo. Đo sơ bộ trên máy Đức (w4 nhanh hơn 20–40%) không lặp lại được ở đây.
- Theo quy tắc trong `tile_shape.conf` ("nếu w4 không nhanh hơn w1 rõ ràng, vượt IQR, trên máy đo thì quay lại tile b × b"): **đề xuất dùng tile b×b (`--variant simd-w1`) cho E5, E6**, đúng với mã mẫu trong tài liệu lý thuyết (mục 6.3).
- Độ tin cậy: 17/54 cấu hình `mm_par_2d` có IQR trên 20%, chủ yếu ở n = 600, 1000 với nhiều thread, khi mỗi lần chạy chỉ mất 5–35 ms.

## 3. Speedup sơ bộ (chưa phải E5)

Dùng tile tốt nhất của mỗi dòng. Mốc tuần tự là `mm_block_seq` b = 48 đo **trong cùng phiên**: 15,1 ms (n = 600), 78,1 ms (n = 1000), 653,5 ms (n = 2000).

| n | Lịch | 1 thread | 6 thread | 12 thread | Efficiency ở 6 thread | 6 thread so với chính tile 1 thread |
|---:|---|---:|---:|---:|---:|---:|
| 600 | static | 0,66 | 2,16 | 2,79 | 0,36 | 3,29 |
| 600 | dynamic | 0,57 | 3,19 | 2,74 | 0,53 | 5,54 |
| 1000 | static | 0,67 | 3,37 | 2,79 | 0,56 | 5,06 |
| 1000 | dynamic | 0,61 | 3,16 | 2,95 | 0,53 | 5,19 |
| 2000 | static | 0,69 | **3,70** | 3,21 | 0,62 | 5,34 |
| 2000 | dynamic | 0,65 | 3,50 | 3,17 | 0,58 | 5,35 |

- Speedup = median `mm_block_seq` / median `mm_par_2d`. Efficiency = speedup / số thread.
- **Speedup tối đa 3,7 lần ở 6 thread.** Một phần lớn bị mất ngay ở 1 thread: bản tile chỉ đạt 0,57–0,69 lần tốc độ bản tuần tự (mục 4). So với chính bản tile chạy 1 thread thì 6 thread nhanh hơn khoảng 5,1–5,5 lần, efficiency khoảng 0,9.
- **12 thread không nhanh hơn 6 thread.** Kernel FMA đã dùng hết đơn vị tính của mỗi lõi vật lý, nên 2 thread SMT trên cùng lõi không thêm được phép tính, mà còn chia nhau L1, L2.
- Đây chỉ là số sơ bộ để định hướng. Speedup chính thức lấy từ E5 (C7).

## 4. Review D5: bản tile chạy 1 thread chậm hơn bản tuần tự

**Đúng đắn:** không có race condition (mỗi tile chỉ ghi vùng C của nó, vòng `kk` tuần tự trong tile), kết quả giống từng bit `mm_block_seq` cùng b, `test_block_par` qua 1–12 thread trên máy đo.

**Hiệu năng:** ở 1 thread, `mm_par_2d` chậm hơn `mm_block_seq` cùng b, mọi n, mọi bề rộng tile (mục 3). Cường đo tay thêm để tìm nguyên nhân. Các lần đo này không qua script nên không có trong `results/raw/`. n = 1000, b = 48, 1 thread, không ghim thread:

| Bản | Các lần đo (ms) | Median | So với `mm_block_seq` |
|---|---|---|---|
| `mm_block_seq` (simd) | 70,0 / 73,6 / 73,9 / 73,4 / 74,3 | 73,6 | 1 |
| `mm_par_2d` (simd, tile b×4b) | 101,0 / 101,5 / 100,8 / 101,3 / 103,8 | 101,3 | **1,38 lần chậm hơn** |
| `mm_par_2d --variant simd-w64` (tile rộng cả hàng) | 100,4 / 107,5 / 101,9 / 101,9 / 106,2 | 101,9 | **1,38 lần chậm hơn** |
| `mm_block_seq --variant novec` | 198,0 / 200,3 / 204,1 | 200,3 | 1 |
| `mm_par_2d --variant novec` | 259,7 / 265,8 / 269,9 | 265,8 | **1,33 lần chậm hơn** |

Lệnh để chạy lại (MSYS2 UCRT64):

```bash
for r in 1 2 3 4 5; do
  ./build/mm_block_seq --n 1000 --b 48 | grep -o 't_compute=[0-9.]*'
  ./build/mm_par_2d --n 1000 --b 48 --threads 1 | grep -o 't_compute=[0-9.]*'
  ./build/mm_par_2d --n 1000 --b 48 --threads 1 --variant simd-w64 | grep -o 't_compute=[0-9.]*'
done
```

**Nhận định:**
- Hai nhóm số không chồng nhau: lần nhanh nhất của bản tile (100,8 ms) vẫn chậm hơn lần chậm nhất của bản tuần tự (74,3 ms).
- **Không phải do hình dạng tile:** tile rộng cả hàng (`simd-w64`) duyệt đúng thứ tự `ii, kk, jj` như `multiply_blocked` mà vẫn chậm như vậy.
- **Không phải do ghim thread:** trong `tile_shape` bản tile có ghim (`--pin 1`, 117–142 ms ở n = 1000), các lần đo tay ở trên thì không ghim (101 ms), và cả hai đều chậm hơn `mm_block_seq` (73,6–78,1 ms).
- **Không riêng phần vector hóa:** kernel `novec` cũng chậm 1,33 lần. Báo cáo `-fopt-info-vec-optimized` của GCC cho thấy kernel được vector hóa bằng lệnh 32 byte ở cả `mm_block_seq.cpp` và `mm_par_2d.cpp`.
- Nhiều khả năng do cách GCC tách thân vùng `omp parallel` thành hàm riêng làm mất một số tối ưu (truyền con trỏ A, B, C và các biên vòng lặp qua cấu trúc dữ liệu chung của vùng song song). **Chưa xác định được chính xác.** Hướng thử: gọi một hàm riêng cho mỗi tile (ví dụ đánh dấu `noinline`), hoặc chép A, B, C, n, b vào biến cục bộ `const` ngay trong vùng song song trước vòng lặp.
- `mm_par_1d` (V9, Vũ) gọi cùng kernel từ trong vùng song song nên nhiều khả năng cũng gặp vấn đề này; nên đo `--threads 1` so với `mm_block_seq` khi có bản 1D.

## 5. Lưu ý về điều kiện máy giữa các phiên đo

Cùng `mm_block_seq` b = 48, code không đổi (D5 không sửa kernel), nhưng phiên đo này nhanh hơn E3 (22/11, 21:50) rõ rệt:

| n | E3 (22/11) | tile_shape (23/11) | Phiên sau nhanh hơn |
|---:|---:|---:|---:|
| 600 | 54,4 ms | 15,1 ms | 3,6 lần |
| 1000 | 123 ms | 78,1 ms | 1,6 lần |
| 2000 | 876 ms | 653,5 ms | 1,3 lần |

- Trạng thái máy (chế độ nguồn, cắm sạc, nhiệt, ứng dụng nền) thay đổi giữa các phiên và ảnh hưởng mạnh tới số đo, nhất là ở n nhỏ.
- **Không so số liệu giữa các phiên khác nhau.** Speedup chỉ tính khi mốc tuần tự và bản song song đo trong cùng một phiên, như mục 3.
- Đề xuất cho C7: chạy lại E2, E3 trong cùng phiên với E1, E4–E7, và ghi chế độ nguồn, trạng thái cắm sạc vào `meta.txt`.
- Hệ quả cho E3: kết luận b = 48 vẫn giữ, vì E3 so các b với nhau trong cùng một phiên. Nhưng con số GFLOP/s tuyệt đối của E3 thấp hơn khả năng thật của máy.
