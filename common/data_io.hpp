#pragma once

// Đọc/ghi file dữ liệu theo định dạng chung trong README (mục "Định dạng file dữ liệu").
//
//   Đầu vào tích vô hướng:  dot <n>     rồi n số của x, n số của y
//   Đầu vào nhân ma trận:   matmul <n>  rồi n*n số của A (theo hàng), n*n số của B
//   Kết quả:                dot_result <giá trị>   hoặc   matrix <n> rồi n dòng của C
//
// Bộ đọc coi mọi khoảng trắng (cách, tab, xuống dòng LF hoặc CRLF) như nhau, chỉ đếm số.
// Thiếu số, thừa số, số không hợp lệ, NaN/Inf, sai từ khóa đầu file đều là lỗi:
// ném std::runtime_error, thông báo kèm tên file và số dòng.

#include <string>

#include "common/cli.hpp"
#include "common/matrix.hpp"
#include "common/timer.hpp"

// Đọc file đầu vào; x, y (vector 1 x n) hoặc A, B (n x n) được cấp phát lại theo n trong file.
void read_dot_input(const std::string& path, Matrix& x, Matrix& y);
void read_matmul_input(const std::string& path, Matrix& A, Matrix& B);

// Ghi kết quả, số ghi bằng %.17g để đọc lại không mất chính xác.
void write_dot_result(const std::string& path, double value);
void write_matrix_result(const std::string& path, const Matrix& C);

// Lấy dữ liệu đầu vào theo tham số dòng lệnh, dùng chung cho mọi chương trình:
//   - có --input: đọc file, thời gian tính vào pha IO; nếu có cả --n thì phải khớp n trong file;
//   - không có:   sinh theo --n, --seed bằng generate_*_input, thời gian tính vào pha Init.
// Trả về n.
long long load_dot_input(const Options& opt, Matrix& x, Matrix& y, pc::PhaseTimer& timer);
long long load_matmul_input(const Options& opt, Matrix& A, Matrix& B, pc::PhaseTimer& timer);
