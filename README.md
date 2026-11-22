# Lập trình tính toán song song — Đồ án nhóm 2026-1

Kho mã nguồn đồ án nhóm môn Lập trình tính toán song song, gồm hai bài:

| Bài | Nội dung |
|---|---|
| Bài chung | Tính tích vô hướng của hai vector |
| Bài riêng (Chủ đề 5) | Nhân ma trận theo khối, tối ưu locality: so sánh bản tuần tự chuẩn với bản chia block/tile, khảo sát block size, thứ tự vòng lặp, vectorization/shared memory và ít nhất hai cách phân hoạch |

> **Trạng thái:** đã dựng khung kho mã và giao diện thử nghiệm (`ui/`). Các file nguồn C++ hiện là stub, chưa có thuật toán và chưa có kết quả đo.

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
│   ├── cli.hpp             Đọc tham số dòng lệnh theo hợp đồng
│   ├── timer.hpp           Đo thời gian theo pha: khởi tạo, tính toán, I/O
│   ├── matrix.hpp/.cpp     Cấp phát căn lề, sinh dữ liệu theo seed
│   ├── data_io.hpp/.cpp    Đọc/ghi file dữ liệu định dạng chung
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
├── tests/                  Kiểm thử (test_*.cpp), chạy bằng make test
├── docs/                   Ghi chú phân tích (vd. phan_tich_thu_tu_vong_lap.md cho E2)
├── scripts/
│   ├── run_experiments.sh  Chạy ma trận cấu hình, mỗi cấu hình ít nhất 5 lần
│   ├── plot_results.py     Tính median, IQR, speedup, efficiency và vẽ biểu đồ
│   └── make_samples.py     Sinh dữ liệu mẫu có đáp án vào data/sample/
├── data/
│   └── sample/             Dữ liệu mẫu nhỏ; dữ liệu lớn được sinh theo seed
│       └── expected/       Đáp án của từng mẫu
├── results/
│   ├── raw/                CSV/JSON thô từ thí nghiệm
│   └── figures/            Biểu đồ sinh từ raw/
│
├── ui/                     Giao diện thử nghiệm (không dùng để đo số liệu báo cáo)
│   ├── server.py           Server cục bộ, chỉ dùng thư viện chuẩn của Python
│   ├── programs.json       Khai báo các chương trình hiện trên giao diện
│   └── static/             index.html, app.js, style.css
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
make test
```

Mỗi file `.cpp` trong `dot_product/`, `matmul/`, `tests/` thành một chương trình `build/<tên>.exe`. `make test` build rồi chạy lần lượt mọi `build/test_*.exe`, dừng ở test đầu tiên thất bại. Chạy `make test` trước khi tạo pull request. Đổi cờ biên dịch khi thí nghiệm:

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

## Giao diện thử nghiệm

Giao diện web chạy trên máy mình để thử nhanh các bản tuần tự và song song trên cùng một bộ dữ liệu, xem kết quả và so sánh. **Chỉ dùng để thử và gỡ lỗi**; số liệu đưa vào báo cáo phải đo bằng `scripts/run_experiments.sh`.

**Chạy:**

1. Build trong MSYS2 UCRT64: `make`.
2. Trong PowerShell, ở thư mục gốc repo:

   ```powershell
   python ui/server.py --open
   ```

3. Trình duyệt mở http://127.0.0.1:8000. Dừng server bằng Ctrl+C.

Không cần cài thêm thư viện Python nào. Server tự thêm `C:\msys64\ucrt64\bin` vào PATH khi chạy chương trình, nên chương trình OpenMP tìm được `libgomp-1.dll`.

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `--port` | `8000` | Cổng lắng nghe; server chỉ nhận kết nối từ chính máy này (127.0.0.1) |
| `--build-dir` | `build` | Thư mục chứa file `.exe`, tương đối với thư mục gốc repo |
| `--msys2-bin` | `C:\msys64\ucrt64\bin` | Thư mục DLL của MSYS2 UCRT64 |
| `--timeout` | `120` | Giới hạn thời gian mỗi lần chạy, tính bằng giây |
| `--open` | tắt | Tự mở trình duyệt |

**Cách dùng:**

1. Chọn tab **Tích vô hướng** hoặc **Nhân ma trận**.
2. Chọn nguồn dữ liệu:
   - **Sinh theo seed:** chương trình tự sinh dữ liệu, dùng cho kích thước lớn.
   - **File mẫu:** lấy từ `data/sample/`.
   - **Tải file lên:** `.txt` theo định dạng chung, hoặc `.csv`.
   - **Nhập tay:** có nút sinh số nguyên nhỏ ngẫu nhiên, để kết quả chính xác tuyệt đối.
3. Đặt tham số trên từng thẻ chương trình. Bấm **Chạy**, hoặc chọn nhiều bản rồi bấm **Chạy các bản đã chọn**; bản tuần tự luôn chạy trước để làm mốc.
4. Bảng kết quả hiển thị:
   - thời gian từng pha và `check`;
   - tỉ lệ tăng tốc so với bản tuần tự nhanh nhất trên cùng dữ liệu (chỉ để tham khảo);
   - đánh dấu `≠` khi `check` lệch so với bản tuần tự đó.

   Bấm **Chi tiết** để xem lệnh tương đương (chạy lại được trong terminal), stdout/stderr, kết quả và link tải `output.txt`.

Thẻ chương trình hiện **"chưa build"** khi chưa có `build/<id>.exe`. Sau khi build, quay lại trình duyệt là thẻ tự cập nhật.

## Hợp đồng tích hợp chương trình C++

Giao diện không cần biết bên trong chương trình. Server chỉ gọi `build/<id>.exe` với các tham số dưới đây, rồi đọc file CSV và file kết quả mà chương trình ghi ra. **Muốn chương trình của mình hiện trên giao diện, chỉ cần theo đúng hợp đồng này và khai báo trong `ui/programs.json`; không phải viết mã giao diện.**

### Tham số dòng lệnh

| Tham số | Ý nghĩa | Ghi chú |
|---|---|---|
| `--n <số>` | Kích thước vector hoặc cấp ma trận | Dùng khi sinh dữ liệu theo seed |
| `--seed <số>` | Seed sinh dữ liệu | Mặc định 42 |
| `--input <file>` | Đọc dữ liệu từ file theo định dạng chung | Khi có tham số này, `n` lấy từ file và bỏ qua `--n`, `--seed` |
| `--output <file>` | Ghi kết quả ra file theo định dạng chung | |
| `--csv <file>` | Ghi thêm **đúng một dòng** kết quả đo | Ghi header nếu file chưa có |
| `--b <số>` | Block size | Chỉ bản có chia block |
| `--threads <số>` | Số thread OpenMP | Chỉ bản song song |
| `--variant <tên>` | Biến thể, ví dụ `ijk`, `ikj`; `barrier`, `nobarrier` | Chỉ bản có biến thể |
| `--schedule static\|dynamic` | Lập lịch OpenMP | Chỉ bản song song có dùng |
| `--pin 0\|1` | Ghim thread vào lõi vật lý | Chỉ bản song song |
| `--run <số>` | Số thứ tự lần chạy, ghi vào CSV | |
| `--verify` | So kết quả với bản tham chiếu | Sai thì trả mã thoát khác 0 |

**Quy ước tính thời gian các pha:**
- Đọc `--input` và ghi `--output` tính vào `t_io`.
- Cấp phát và sinh dữ liệu theo seed tính vào `t_init`.
- Chỉ phần tính toán tính vào `t_compute`.

### Định dạng file dữ liệu chung

File văn bản; các số cách nhau bởi khoảng trắng, viết dạng thập phân thông thường (`3`, `-1.5`, `2e-3`). Định dạng này dùng chung cho giao diện, `data/sample/` và chương trình C++.

**Tích vô hướng:** dòng đầu `dot <n>`, tiếp theo một dòng gồm n số của x, rồi một dòng gồm n số của y.

```text
dot 4
1 2 3 4
5 6 7 8
```

**Nhân ma trận** (ma trận vuông): dòng đầu `matmul <n>`, tiếp theo n dòng của A, rồi n dòng của B.

```text
matmul 2
1 2
3 4
5 6
7 8
```

**Kết quả** (`--output`):
- Tích vô hướng: một dòng `dot_result <giá trị>`.
- Nhân ma trận: dòng `matrix <n>`, tiếp theo n dòng của C.
- Ghi số bằng `%.17g` để đọc lại không mất độ chính xác.

```text
dot_result 70
```

```text
matrix 2
19 22
43 50
```

Khi tải `.csv` lên, giao diện tự chuyển về định dạng này:
- tích vô hướng: 2 hàng (x, y) hoặc 2 cột;
- nhân ma trận: hai file `A.csv` và `B.csv`.

Dòng tiêu đề nếu có sẽ được bỏ qua. Vì vậy chương trình C++ chỉ cần đọc một định dạng.

### Dữ liệu mẫu có đáp án (`data/sample/`)

Mỗi mẫu `data/sample/<tên>.txt` có đáp án `data/sample/expected/<tên>.txt` theo định dạng kết quả ở trên. Mẫu chỉ chứa số nguyên nhỏ, nên mọi bản (tuần tự, block, song song, mọi thứ tự cộng) phải ra **đúng** đáp án, và file `--output` trùng từng byte với file đáp án.

| Mẫu | Mục đích |
|---|---|
| `dot_4`, `matmul_2` | Ví dụ ở trên: 70 và `19 22 / 43 50` |
| `dot_1`, `matmul_1` | Biên n = 1 |
| `dot_1000` | n không chia hết cho độ rộng vector |
| `matmul_identity_6` | B là ma trận đơn vị, đáp án C = A |
| `matmul_17` | n lẻ: block ở biên bị cắt với mọi b |
| `matmul_64` | Cỡ vừa, n chia hết cho các block thường dùng |

- `tests/test_samples.cpp` (chạy trong `make test`) tính mọi mẫu bằng các kernel của cả nhóm và so với đáp án. Thêm kernel mới thì thêm vào test này.
- Thử nhanh một chương trình: `./build/mm_seq.exe --input data/sample/matmul_17.txt --output out.txt`, rồi so `out.txt` với `data/sample/expected/matmul_17.txt`.
- Sinh lại hoặc thêm mẫu: sửa và chạy `python scripts/make_samples.py` (trong PowerShell). Đáp án tính bằng số nguyên Python, độc lập với mã C++.

### Dòng CSV kết quả

```text
problem,variant,n,block,threads,sched,pinned,run,seed,t_init,t_compute,t_io,t_total,check
```

- Thời gian tính bằng giây.
- `check` là giá trị kiểm tra kết quả: tích vô hướng thì là chính kết quả; nhân ma trận thì là tổng các phần tử của C.
- Giao diện dùng `check` để phát hiện bản song song lệch so với bản tuần tự.

### Mã thoát

- `0` khi thành công.
- Khác `0` khi lỗi: tham số sai, file sai định dạng, hoặc `--verify` phát hiện kết quả sai. Đồng thời in thông báo lỗi ra **stderr**; giao diện hiển thị nguyên văn thông báo này.

### Khai báo trong `ui/programs.json`

Mỗi chương trình là một mục trong mảng `programs`. Server đọc lại file mỗi lần, nên sửa xong chỉ cần tải lại trang.

```json
{
  "id": "mm_par_2d",
  "problem": "matmul",
  "kind": "par",
  "label": "Song song theo tile",
  "owner": "Đức",
  "params": ["b", "threads", "schedule", "pin"]
}
```

| Trường | Bắt buộc | Ý nghĩa |
|---|---|---|
| `id` | Có | Tên file chạy, không có `.exe`: phải khớp tên file `.cpp` |
| `problem` | Có | `dot` hoặc `matmul`: tab hiển thị |
| `kind` | Có | `seq` hoặc `par`: cột Tuần tự hay Song song |
| `label`, `owner` | Có | Tên hiển thị và người phụ trách |
| `params` | Có | Tham số riêng mà chương trình nhận, chọn trong `b`, `threads`, `variant`, `schedule`, `pin`. `--n`, `--seed`, `--input`, `--output`, `--csv`, `--run`, `--verify` là tham số chung, không khai báo |
| `variants` | Khi có `variant` | Danh sách giá trị cho `--variant` |
| `threads` | Không | Giới hạn số thread được chọn, ví dụ Cannon chỉ nhận `[1, 4, 9]` |

### Checklist để chương trình của bạn hiện trên giao diện

1. File `.cpp` nằm trong `dot_product/` hoặc `matmul/`, build ra `build/<id>.exe` bằng `make`.
2. Nhận đủ các tham số chung trong bảng trên, cùng các tham số riêng đã khai báo.
3. Đọc được `--input` và ghi được `--output` bằng các hàm dùng chung trong `common/data_io` (xem mục dưới). Không tự viết bộ đọc riêng.
4. Ghi đúng một dòng CSV vào `--csv`, theo đúng thứ tự cột.
5. Trả mã thoát khác 0 và in lỗi ra stderr khi có sự cố.
6. Thêm hoặc sửa mục của mình trong `ui/programs.json`.
7. Mở giao diện, chạy với dữ liệu nhập tay nhỏ (nút "Sinh số nguyên nhỏ ngẫu nhiên"), so với bản tuần tự. `check` phải khớp và không có cảnh báo.

Giao diện báo **"Xong, có cảnh báo"** khi chương trình không ghi CSV hoặc file kết quả, thường là do vẫn còn là stub. Bấm **Chi tiết** để xem lệnh mà server đã chạy, rồi chạy lại đúng lệnh đó trong terminal để gỡ lỗi.

### Mã dùng chung trong `common/` (Vũ phụ trách)

Chi tiết từng hàm nằm trong chú thích của file `.hpp`.

| File | Dùng để |
|---|---|
| `cli.hpp` | `parse_options_or_exit(argc, argv)` trả về `Options` chứa mọi tham số trong bảng trên; tham số sai thì in lỗi và thoát mã 2 |
| `matrix.hpp` | Lớp `Matrix`: lưu theo hàng, căn lề 64 byte, `A(i, j)` hoặc `A.data()[i * n + j]`; vector độ dài n là `Matrix` 1 x n. Không sao chép ngầm được, cần bản sao thì gọi `clone()` |
| `data_io.hpp` | `load_dot_input` / `load_matmul_input`: lấy dữ liệu đầu vào theo `--input` hoặc `--n`, `--seed`. `write_dot_result` / `write_matrix_result`: ghi `--output` |
| `csv_log.hpp` | `make_csv_record` điền đủ 14 cột từ `Options` và timer; `append_csv_row` ghi một dòng vào `--csv` |
| `verify.hpp` | Nhân ma trận: `mm::verify_product(A, B, C)` cho `--verify` (n ≤ 512 so từng phần tử với bản tham chiếu, lớn hơn dùng Freivalds O(n²)); `mm::describe` in kết quả; `mm::checksum(C)` cho cột `check` |
| `timer.hpp` | `pc::PhaseTimer` đo ba pha init, compute, io (Cường) |

**Quy ước chung, để kết quả các bản so sánh được với nhau:**
- Lấy dữ liệu đầu vào **chỉ bằng** `load_dot_input` / `load_matmul_input`, không tự sinh hay tự đọc.
  - Có `--input` thì đọc file, thời gian tính vào `t_io`. Có cả `--n` mà khác n trong file thì báo lỗi.
  - Không có `--input` thì sinh theo `--n`, `--seed` (số thực đều trong [-1, 1)), thời gian tính vào `t_init`.
  - Cùng `--n`, `--seed` thì mọi chương trình nhận đúng cùng dữ liệu, trên mọi máy.
- Chỉ đo phần tính toán trong pha `Compute`.
- Không truyền `--variant` thì gán `rec.variant` bằng tên biến thể mặc định trước khi ghi CSV.
- Bắt mọi ngoại lệ trong `main`, in ra stderr, trả mã thoát 2.
- Test đúng đắn dùng `Fill::SmallInt` (số nguyên trong [-4, 4]): kết quả tuần tự và song song phải khớp từng bit.

**Khung `main` mẫu** (tích vô hướng; nhân ma trận tương tự với `load_matmul_input`, `write_matrix_result`):

```cpp
#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/timer.hpp"
#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    pc::PhaseTimer timer;
    const Options opt = parse_options_or_exit(argc, argv);
    try {
        Matrix x, y;
        const long long n = load_dot_input(opt, x, y, timer);

        double result = 0.0;
        {
            auto scope = timer.measure(pc::Phase::Compute);
            result = /* tính tích vô hướng */;
        }

        if (!opt.output.empty()) {
            auto scope = timer.measure(pc::Phase::IO);
            write_dot_result(opt.output, result);
        }
        if (!opt.csv.empty()) {
            CsvRecord rec = make_csv_record("dot", opt, n, timer, result);
            if (rec.variant.empty()) rec.variant = "seq";
            append_csv_row(opt.csv, rec);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
```

## Chạy thí nghiệm

Số liệu báo cáo chỉ đo bằng `scripts/run_experiments.sh` (Cường phụ trách), chạy trong **MSYS2 UCRT64** ở thư mục gốc repo.

```bash
scripts/run_experiments.sh --check                                  # kiểm tra toolchain: g++, OpenMP đủ thread, make test
scripts/run_experiments.sh --dry-run scripts/experiments/e1_dot.conf  # xem các lệnh sẽ chạy
scripts/run_experiments.sh scripts/experiments/e1_dot.conf            # chạy thật
```

**Script làm gì:**
1. `make` (và build riêng cho từng dòng `@build`).
2. Chạy mỗi cấu hình một lần với `--verify`, không ghi CSV. Có cấu hình sai thì dừng, không đo.
3. Mỗi cấu hình chạy 5 lần (`-r N` để đổi), thứ tự mọi lần chạy được xáo trộn. Mỗi lần ghi một dòng vào `results/raw/<tên thí nghiệm>.csv`.
4. Bị ngắt giữa chừng thì chạy lại đúng lệnh cũ, script bỏ qua các lần đã xong (ghi trong `.done`). `--fresh` xóa kết quả cũ để chạy lại từ đầu.

**File sinh ra trong `results/raw/`:**

| File | Nội dung |
|---|---|
| `<tên>.csv` | Một dòng mỗi lần chạy, theo định dạng CSV chung |
| `<tên>.<build>.csv` | Như trên, cho các dòng sau `@build <build>` (bản build với cờ khác) |
| `<tên>.meta.txt` | Mỗi phiên chạy: thời điểm, commit, CPU, RAM, compiler, cờ, số lần, seed xáo trộn |
| `<tên>.log` | Các lần chạy lỗi kèm thông báo (chỉ có khi có lỗi) |
| `<tên>.done` | Các lần đã chạy xong, dùng để chạy tiếp; không đưa lên Git |

**File cấu hình** (`scripts/experiments/*.conf`, tên file là tên thí nghiệm), mỗi dòng một cấu hình:

```text
# chú thích
dot_omp --variant {plain,simd} --n {8192,100000000} --threads {1,2,4,6,12}
@build novec -std=c++17 -O3 -fno-tree-vectorize -Wall -Wextra -fopenmp
mm_block_seq --n 1024 --b {32,64}
@build default
```

- `{a,b,c}` được khai triển thành mọi tổ hợp. Không ghi `--run`, `--csv`, vì script tự thêm.
- `@build TÊN CỜ...`: các dòng sau dùng bản build vào `build/TÊN` với cờ đó. `@build default` quay lại `build/`.

| Tùy chọn | Mặc định | Ý nghĩa |
|---|---|---|
| `-r N` | `5` | Số lần chạy mỗi cấu hình |
| `-o DIR` | `results/raw` | Thư mục kết quả |
| `-s SEED` | `2026` | Seed xáo trộn thứ tự chạy |
| `-p GIÂY` | `0` | Nghỉ giữa hai lần chạy |
| `-t GIÂY` | `0` (không giới hạn) | Giới hạn thời gian mỗi lần chạy |
| `--no-verify` | | Bỏ lượt kiểm tra `--verify` |
| `--fresh` | | Xóa kết quả cũ của thí nghiệm rồi chạy lại |
| `--dry-run` | | Chỉ in lệnh, không chạy |

`scripts/experiments/smoke.conf` chỉ để chạy thử quy trình trong vài giây. Kết quả của nó không dùng cho báo cáo và không đưa lên Git.

**Trước khi chạy thí nghiệm chính:** cắm sạc, cố định chế độ nguồn, đóng ứng dụng nền, không đặt các biến `OMP_*` (`--check` sẽ cảnh báo nếu có).

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
