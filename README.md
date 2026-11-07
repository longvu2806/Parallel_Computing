# Lập trình tính toán song song — Đồ án nhóm 2026-1

Kho mã nguồn đồ án nhóm môn Lập trình tính toán song song, gồm hai bài:

| Bài | Nội dung |
|---|---|
| Bài chung | Tính tích vô hướng của hai vector |
| Bài riêng (Chủ đề 5) | Nhân ma trận theo khối, tối ưu locality: so sánh bản tuần tự chuẩn với bản chia block/tile, khảo sát block size, thứ tự vòng lặp, vectorization/shared memory và ít nhất hai cách phân hoạch |

> **Trạng thái:** đã dựng khung kho mã. Các file nguồn hiện là stub, chưa có thuật toán và chưa có kết quả đo.

## Công nghệ

- C++17, song song hóa bằng OpenMP.
- GCC (MinGW-w64, 64-bit) và GNU Make trên Windows, qua môi trường MSYS2 UCRT64.
- Python với NumPy, pandas, Matplotlib để xử lý số liệu và vẽ biểu đồ.

## Cấu trúc thư mục

```text
parallel_computing/
├── README.md               Giới thiệu và hướng dẫn tái lập
├── .gitignore
├── .gitattributes          Giữ LF cho script và Makefile
├── Makefile                Build toàn bộ chương trình vào build/
├── AI_USAGE.md             Nhật ký sử dụng AI (bắt buộc)
├── CONTRIBUTIONS.md        Đóng góp của từng thành viên (bắt buộc)
│
├── common/                 Mã dùng chung cho cả hai bài
│   ├── timer.hpp           Đo thời gian theo pha: khởi tạo, tính toán, I/O
│   ├── matrix.hpp/.cpp     Cấp phát căn lề, sinh dữ liệu theo seed
│   └── csv_log.hpp/.cpp    Ghi kết quả đo ra CSV
│
├── dot_product/            Bài chung
│   ├── dot_seq.cpp         Bản tuần tự
│   └── dot_omp.cpp         Bản song song
│
├── matmul/                 Bài riêng
│   ├── mm_seq.cpp          Tuần tự chuẩn, các thứ tự vòng lặp (ijk, ikj, ...)
│   ├── mm_block_seq.cpp    Tuần tự chia block/tile
│   ├── mm_par_1d.cpp       Song song, phân hoạch 1D theo hàng
│   ├── mm_par_2d.cpp       Song song, phân hoạch 2D theo tile
│   └── mm_kernel.hpp       Vi nhân cho một block, có vectorization
│
├── tests/                  Kiểm thử: so bản song song với bản tuần tự
├── scripts/
│   ├── run_experiments.sh  Chạy ma trận cấu hình, mỗi cấu hình ít nhất 5 lần
│   └── plot_results.py     Tính median, IQR, speedup, efficiency và vẽ biểu đồ
├── data/
│   └── sample/             Dữ liệu mẫu nhỏ; dữ liệu lớn được sinh theo seed
├── results/
│   ├── raw/                CSV/JSON thô từ thí nghiệm
│   └── figures/            Biểu đồ sinh từ raw/
│
└── docx/                   Tài liệu nội bộ, không đưa lên Git
```

## Quick setup

Hướng dẫn cho Windows 10/11 x64. Các bước 1–3 chỉ cần làm một lần trên mỗi máy.

**1. Cài MSYS2** (trong PowerShell):

```powershell
winget install -e --id MSYS2.MSYS2
```

**2. Cài compiler.** Mở **MSYS2 UCRT64** từ Start Menu rồi chạy:

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-libgomp make
```

Nếu `pacman -Syu` tự đóng cửa sổ, hãy mở lại MSYS2 UCRT64, chạy lại lệnh đó rồi chạy tiếp lệnh thứ hai. Gói `libgomp` là bắt buộc: thiếu gói này sẽ gặp lỗi `cannot find -lgomp`.

**3. Kiểm tra** (vẫn trong MSYS2 UCRT64):

```bash
gcc -dumpmachine
echo 'int main(){}' | g++ -fopenmp -x c++ - -o omp_check.exe && ./omp_check.exe && echo "OpenMP OK"; rm -f omp_check.exe
```

Kết quả cần là `x86_64-w64-mingw32` và `OpenMP OK`.

**4. Build** (trong MSYS2 UCRT64, ổ `C:` là `/c`):

```bash
cd /c/duong/dan/toi/parallel_computing
make
./build/mm_seq.exe
```

Mỗi file `.cpp` trong `dot_product/`, `matmul/`, `tests/` thành một chương trình `build/<tên>.exe`. Đổi cờ biên dịch khi thí nghiệm:

```bash
make clean && make CXXFLAGS="-std=c++17 -O2 -fopenmp"
```

**5. Cài thư viện Python** (trong PowerShell, cần có sẵn Python):

```powershell
pip install numpy pandas matplotlib
```

### Lưu ý: chạy lệnh ở đâu

| Việc | Terminal |
|---|---|
| `make`, chạy chương trình, `scripts/run_experiments.sh` | MSYS2 UCRT64 |
| `git`, `python`, `pip` | PowerShell hoặc terminal của VS Code |

- Shell UCRT64 không thấy `git` và `python` đã cài trên Windows.
- Chương trình dùng OpenMP cần `libgomp-1.dll` của MSYS2, nên có thể không chạy được từ PowerShell nếu `C:\msys64\ucrt64\bin` không nằm trong PATH.
- Không dùng MinGW.org ở `C:\MinGW`: bản này 32-bit và không link được OpenMP. Nếu máy còn bản này trong PATH, `gcc` trong PowerShell có thể gọi nhầm nó.

## Chạy thí nghiệm

Sẽ được bổ sung khi có `scripts/run_experiments.sh`.

## Môi trường thí nghiệm

Cờ tối ưu dùng cho từng thí nghiệm sẽ được ghi lại cùng kết quả đo.

| Thành phần | Giá trị |
|---|---|
| CPU | AMD Ryzen 7 8845H, 8 lõi / 16 luồng |
| RAM | 27,8 GB |
| Hệ điều hành | Windows 11 Home |
| Compiler | GCC 16.2.0 (MSYS2 UCRT64, Rev6) |
| Build | GNU Make 4.4.1 |
| Cờ mặc định | `-std=c++17 -O3 -march=native -Wall -Wextra -fopenmp` |

## Thư viện và mã nguồn ngoài

Chưa sử dụng. Mọi thư viện hoặc đoạn mã ngoài sẽ được ghi nguồn và giấy phép tại đây.
