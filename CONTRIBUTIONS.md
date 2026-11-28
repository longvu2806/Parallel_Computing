# Đóng góp của thành viên

## Thành viên

| Họ tên | MSSV | Tài khoản Git |
|---|---|---|
| Trần Long Vũ |  | `longvu2806` |
| Cường |  | `xuancuong045` |
| Đức |  | `minhduc05-05` |

## Phân công

| Hạng mục | Người thực hiện | Commit/PR liên quan |
|---|---|---|
| Quản lý repo, review và gộp PR, `make test` | Vũ | PR #2–#21 |
| Mã dùng chung: tham số dòng lệnh, `Matrix`, đọc/ghi dữ liệu, CSV, kiểm tra kết quả (`common/cli`, `matrix`, `data_io`, `csv_log`, `verify`) | Vũ | `1b38f54`, `7a103cb`, `1fba286`, `5a88b65` |
| Mã dùng chung: đo thời gian theo pha (`common/timer.hpp`) | Cường | `9e5ea10` |
| Mã dùng chung: ghim thread (`common/affinity`) | Đức | `dcf2218` |
| Bài chung: tích vô hướng tuần tự và OpenMP (`dot_product/`) | Cường | `cda0154` |
| Nhân ma trận chuẩn, 6 thứ tự vòng lặp (`mm_seq`) | Vũ | `5a88b65` |
| Nhân ma trận song song 1D theo dải hàng (`mm_par_1d`) | Vũ | `4d8e63c` |
| Kernel block và bản chia block tuần tự (`mm_kernel.hpp`, `mm_block_seq`) | Đức | `52256ed`, `fbc20f9` |
| Nhân ma trận song song tile 2D và Cannon (`mm_par_2d`, `mm_cannon`) | Đức | `191aa38`, `bbb2d19` |
| Kiểm thử phần dùng chung, nhân ma trận tuần tự, 1D, dữ liệu mẫu có đáp án (`test_common`, `test_matmul`, `test_par_1d`, `test_samples`) | Vũ | `bff9f7e`, `5a88b65`, `b82db5b`, `d92998b` |
| Kiểm thử timer, tích vô hướng (`test_timer`, `test_dot`) | Cường | `9e5ea10`, `e7e297b` |
| Kiểm thử ghim thread, kernel, tile, Cannon (`test_affinity`, `test_mm_kernel`, `test_block_par`) | Đức | `dcf2218`, `52256ed`, `191aa38`, `bbb2d19` |
| Script chạy thí nghiệm, thống kê, biểu đồ (`run_experiments.sh`, `plot_results.py`) | Cường | `03f1b9d`, `24a79ff`, `0e5ee8b` |
| Đo toàn bộ số liệu trên máy đo (E2, E3, tile_shape, thí nghiệm chính C7) | Cường | `fe8a8c1`, `bf9cbf9`, `393319f`, `45191e2` |
| Phân tích: thứ tự vòng lặp (Vũ); cache và dải block size (Đức); kết quả E3, tile_shape (Cường) | Vũ, Đức, Cường | `0c114ae`, `effa9b9`, `8e960d4`, `cd81246` |
| Giao diện thử nghiệm (`ui/`) | Vũ | `95966ff` |
| Báo cáo |  |  |
| Slide |  |  |
