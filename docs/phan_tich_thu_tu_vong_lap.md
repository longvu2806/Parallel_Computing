# Phân tích locality của 6 thứ tự vòng lặp nhân ma trận (V8)

Ghi chú lý thuyết cho thí nghiệm **E2** (`mm_seq`, `--variant ijk|ikj|jik|jki|kij|kji`).
Mục đích:
- dự đoán trước thứ tự nào nhanh, chậm và vì sao;
- khi có số liệu E2 (Cường đo), đối chiếu để kiểm chứng hoặc bác bỏ.

**Ghi chú này không chứa số đo nào.** Mọi con số dưới đây là kết quả tính theo mô hình.

Mã nguồn tương ứng: [`matmul/mm_seq.hpp`](../matmul/mm_seq.hpp).

## 1. Mô hình

- A, B, C là ma trận n × n số `double`, lưu **theo hàng**: `X[i][j]` ở địa chỉ `X + 8(i·n + j)` byte.
- Cache line 64 byte = **8 double**. Đọc một phần tử kéo về cả 8 phần tử liền kề cùng hàng.
- Đếm **số miss trên một lần lặp vòng trong cùng**, với giả thiết n đủ lớn để dữ liệu không được giữ lại trong cache giữa hai lần lặp của vòng ngoài. Tổng số miss ≈ hệ số × n³.
- Phương pháp theo Bryant & O'Hallaron, *Computer Systems: A Programmer's Perspective*, 3rd ed., mục 6.6.2. Sách dùng line 32 byte; ở đây tính lại cho line 64 byte.

**Thông số cache của máy đo (i7-9850H, Coffee Lake),** khớp với ghi chú của Đức trong `scripts/experiments/e3_block.conf`:

| Mức | Dung lượng | Số line 64 B | Ghi chú |
|---|---|---|---|
| L1d | 32 KiB, 8-way | 512 | 64 set; địa chỉ cách nhau 4 KiB rơi vào cùng set |
| L2 | 256 KiB, 4-way | 4 096 | Riêng mỗi lõi |
| L3 | 12 MiB | 196 608 | Dùng chung 6 lõi |
| DTLB | 64 mục (trang 4 KiB) | — | STLB 1 536 mục (theo tài liệu tối ưu của Intel cho nhân Skylake, Coffee Lake dùng cùng nhân) |

## 2. Cách truy cập ở vòng trong và số miss

Vòng trong cùng đi theo hàng (bước 1 phần tử) thì cứ 8 lần mới miss 1 lần (1/8). Đi theo cột (bước n phần tử = 8n byte) thì lần nào cũng miss (1). Phần tử cố định thì không miss (0).

| Thứ tự | Vòng trong | A | B | C | Đọc / ghi mỗi lần lặp | **Miss / lần lặp** |
|---|---|---|---|---|---|---|
| ijk, jik | k | hàng: 1/8 | cột: 1 | cố định: 0 | 2 đọc, 0 ghi | **1,125** |
| ikj, kij | j | cố định: 0 | hàng: 1/8 | hàng: 1/8 | 2 đọc, 1 ghi | **0,25** |
| jki, kji | i | cột: 1 | cố định: 0 | cột: 1 | 2 đọc, 1 ghi | **2** |

Tỉ lệ số miss giữa nhóm tốt nhất và nhóm tệ nhất là 2 / 0,25 = **8 lần**. Với line 32 byte trong CS:APP, các hệ số là 1,25 / 0,5 / 2, tỉ lệ chỉ 4 lần. Line càng dài thì lợi thế của truy cập theo hàng càng lớn.

## 3. Khi nào mô hình không còn đúng: các ngưỡng n trên máy đo

Mô hình ở mục 2 giả thiết "không giữ được gì". Với n vừa phải, cache giữ lại được một phần dữ liệu và hệ số giảm.

**(a) Cả ba ma trận vừa cache** (24n² byte):

| Mức | Điều kiện | n tối đa |
|---|---|---|
| L1 | 24n² ≤ 32 KiB | 36 |
| L2 | 24n² ≤ 256 KiB | 104 |
| L3 | 24n² ≤ 12 MiB | 724 |

Khi n ≤ 104, mọi truy cập đều trúng L2. Chênh lệch giữa các thứ tự khi đó chủ yếu do phần tính toán (mục 5), không do bộ nhớ.

**(b) Giữ lại một cột qua hai lần lặp liên tiếp.**
- Cột j và cột j+1 nằm chung line (trừ khi j+1 chia hết cho 8). Nếu n line của cột j còn trong L1 khi vòng ngoài sang cột j+1, miss theo cột giảm từ 1 xuống 1/8.
  - ijk, jik giữ một cột của B: n line ≤ 512, tức **n ≲ 450** (trừ hao chỗ cho một hàng của A).
  - jki, kji giữ hai cột (A và C): 2n ≤ 512, tức **n ≤ 256**.
  - Ở L2 (4 096 line), các ngưỡng tương ứng là n ≲ 3 600 và n ≤ 2 048. Vì vậy với n cỡ vài trăm đến vài nghìn, miss theo cột ở L1 phần lớn **trúng L2**: tốn khoảng 12 chu kỳ chứ chưa phải truy cập RAM.
- **Xung đột set:** các phần tử của một cột cách nhau 8n byte. L1 có 64 set, nên một cột chỉ dùng được 64 / gcd(64, n/8) set, mỗi set 8 line. Với n là bội của 8:
  - n = 200: n/8 = 25, gcd = 1, dùng đủ 64 set (512 line);
  - n = 256: n/8 = 32, chỉ 2 set, tức 16 line. Không giữ nổi cột, dù về dung lượng thì vừa;
  - n = 512, 1024: n/8 là bội của 64, cả cột rơi vào **một** set, tức 8 line.

  Vì vậy **E2 nên tránh n là lũy thừa của 2**, thống nhất với ghi chú của Đức cho E3.

**(c) Giữ lại cả ma trận được quét nhiều lần.**
- ikj quét toàn bộ B một lần cho mỗi i. B được dùng lại giữa các i nếu 8n² vừa cache: L2 khi **n ≤ 181**, L3 khi **n ≤ 1 254**.
- kij quét (đọc và ghi) toàn bộ C một lần cho mỗi k; điều kiện tương tự với C.
- Một hàng có n/8 line, vừa L1 khi n ≤ 4 096, nên luôn giữ được ở mọi cỡ dùng trong E2.

**(d) TLB.**
- Đi theo cột với 8n ≥ 4 KiB (**n ≥ 512**): mỗi phần tử nằm ở một trang khác nhau, nên một cột chiếm n trang, vượt 64 mục DTLB.
- Với n < 512, một cột chiếm khoảng n²/512 trang; vượt 64 mục khi n > 181.
- Các thứ tự đi theo cột (ijk, jik với B; jki, kji với A và C) vì vậy chịu thêm miss TLB.
- Bộ tiền nạp (prefetcher) của L1 không vượt qua ranh giới trang 4 KiB, nên truy cập theo cột khi n ≥ 512 hầu như không được tiền nạp.

## 4. Phân biệt hai thứ tự trong cùng một nhóm

Hai thứ tự cùng nhóm có cùng vòng trong, nhưng khác nhau ở dữ liệu nào được giữ lại qua vòng giữa.

| Cặp | Thứ tự đầu giữ lại | Thứ tự sau giữ lại | Dự đoán khi dữ liệu vượt cache |
|---|---|---|---|
| ikj / kij | ikj: hàng C[i] ở L1 suốt vòng k; mỗi hàng C ghi ra bộ nhớ 1 lần | kij: hàng B[k] ở L1 suốt vòng i; nhưng **đọc và ghi lại toàn bộ C n lần** | ikj nhanh hơn khi C không vừa L3 (n > 724): kij có lưu lượng bộ nhớ khoảng gấp đôi (đọc + ghi C so với chỉ đọc B) |
| ijk / jik | ijk: hàng A[i] ở L1 suốt vòng j | jik: cột B[·][j] ở L1 suốt vòng i (nếu n ≲ 450) | Gần nhau; không có lý do rõ ràng để một bản nhanh hơn hẳn |
| jki / kji | jki: cột C[·][j] suốt vòng k | kji: cột A[·][k] suốt vòng j; **ghi lại C n lần** | jki nhanh hơn một chút, cùng lý do như ikj / kij |

## 5. Phần tính toán: trình biên dịch làm gì với từng vòng trong

Khi dữ liệu nằm trong cache (n nhỏ), tốc độ do phần tính toán quyết định. Đọc mã máy GCC 16 sinh ra cho `matmul/mm_seq.hpp` với `-O3`, cả `-march=skylake` (AVX2, giống máy đo) lẫn `-march=native` trên máy Vũ (Zen 4):

| Thứ tự | Mã máy vòng trong | Hệ quả |
|---|---|---|
| ikj, kij | **FMA vector** (`vfmadd…pd`): 4 double mỗi lệnh với AVX2 | Nhanh nhất khi trong cache |
| jki, kji | FMA vô hướng (`vfmadd…sd`), không vector hóa được (đọc theo cột) | Các lần lặp độc lập nhau (mỗi lần ghi một C[i][j] khác), CPU chạy chồng lấn được |
| ijk, jik | Nhân và cộng tách rời, **phép cộng vào `s` phải đúng thứ tự** | Chuỗi phụ thuộc: lần cộng sau chờ lần cộng trước, khoảng 4 chu kỳ mỗi lần lặp, kể cả khi dữ liệu trong cache |

- GCC không được đổi thứ tự cộng số thực nếu không có `-ffast-math`. Vì vậy vòng của ijk, jik không thể cộng song song nhiều tổng con. Với `-march=skylake`, GCC vector hóa được phép nhân, nhưng các phép cộng vẫn chạy lần lượt.
- Đây cũng là nguyên nhân hiện tượng đã ghi trong `matmul/mm_seq.hpp`: {ijk, jik} lệch {ikj, kij, jki, kji} vài ulp. Nhóm sau dùng FMA (làm tròn một lần), nhóm trước làm tròn tích rồi mới cộng (làm tròn hai lần).
- Cách xem lại trên máy đo: `g++ -O3 -march=native -fopt-info-vec-optimized -I. -c matmul/mm_seq.cpp`.

## 6. Dự đoán để đối chiếu với E2

Thời gian tăng dần, tức thứ tự đứng trước nhanh hơn:

| Vùng n | Dữ liệu nằm ở | Dự đoán | Lý do chính |
|---|---|---|---|
| n ≤ ~100 | L2 | ikj ≈ kij **<** jki ≈ kji **<?** ijk ≈ jik | Mục 5: vector FMA, rồi FMA vô hướng độc lập, rồi chuỗi phụ thuộc. Thứ tự giữa jki và ijk ở vùng này **chưa chắc**; E2 sẽ trả lời |
| ~200 – 700 | L3 | ikj ≈ kij **<** ijk ≈ jik **<** jki ≈ kji | Miss theo cột ở L1 bắt đầu tốn (mục 3b); jki, kji miss hai cột |
| n > 724, nhất là n > 1 254 | RAM | ikj **<** kij **<** ijk ≈ jik **<** jki **<** kji | Mô hình mục 2 đúng nhất; thêm miss TLB (mục 3d) và ghi lại C (mục 4) |

- Ở n lớn, **tỉ lệ thời gian** giữa nhóm chậm nhất và nhanh nhất có thể vượt tỉ lệ số miss (8 lần), vì truy cập theo cột còn mất cả tiền nạp và TLB. Ở n nhỏ, tỉ lệ này nhỏ hơn nhiều.
- Gợi ý cỡ cho E2, tránh lũy thừa của 2:
  - n ≈ 100: mọi dữ liệu trong L2;
  - n ≈ 600: mọi dữ liệu trong L3;
  - n ≈ 1 500: B vượt L3.

  Các thứ tự chậm (jki, kji) chỉ chạy ở hai cỡ đầu, như kế hoạch đã ghi.

### Kết quả E2 (Cường điền sau khi đo)

| n | ijk | ikj | jik | jki | kij | kji | Khớp dự đoán? |
|---|---|---|---|---|---|---|---|
| | | | | | | | |
| | | | | | | | |
| | | | | | | | |

Nếu có thời gian, có thể đo trực tiếp số miss L1 bằng Intel VTune trên máy đo (CPU Intel), để so với hệ số ở mục 2.

## 7. Liên hệ với các phần sau

- **Mặc định của `mm_seq` là `ikj`.** Đây là thứ tự có ít miss nhất và vòng trong vector hóa được. Bản này là mốc T1 khi tính tăng tốc.
- **Kernel block của Đức (`matmul/mm_kernel.hpp`) cũng dùng ikj bên trong block.** Chia block giải quyết chỗ yếu còn lại của ikj: B bị quét lại toàn bộ với mỗi i khi n > 181 (mục 3c). Trong block b × b, phần B cần giữ chỉ còn 8b² byte, vừa L1 khi b ≲ 63 (E3).
- **Các bản song song 1D, tile, Cannon** đều gọi kernel block, nên kế thừa cách truy cập theo hàng của ikj.

## Tài liệu tham khảo

- R. E. Bryant, D. R. O'Hallaron. *Computer Systems: A Programmer's Perspective*, 3rd ed., Pearson, 2016, mục 6.6.2.
- Intel. *Intel 64 and IA-32 Architectures Optimization Reference Manual*, phần vi kiến trúc Skylake (thông số cache, TLB).
- Tài liệu lý thuyết của nhóm, `docx/Ly_thuyet_nen_tang.docx`, mục 5.2 (thứ tự vòng lặp) và 5.3 (chia block).
