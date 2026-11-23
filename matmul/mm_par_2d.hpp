#pragma once

// Nhân ma trận song song OpenMP, phân hoạch 2D theo tile (việc D5).
// Dùng chung cho matmul/mm_par_2d.cpp và tests/test_block_par.cpp.
//
// Chia C thành lưới tile, mỗi tile cao b hàng và rộng w = 4b cột (tile ở biên nhỏ hơn).
// Mỗi tile là một đơn vị việc: một thread gán 0 cho tile đó rồi tính
//     for kk (block b của chiều k, tuần tự)
//         for jj (4 block b trong bề rộng tile)
//             mm::block_multiply(hàng tile, kk, jj)
// Hai tile khác nhau không ghi chung ô nào của C, nên không cần đồng bộ.
// Vòng kk KHÔNG được song song: các kk khác nhau cùng cộng vào một tile C (race condition,
// tài liệu lý thuyết mục 6.3).
//
// Vì sao tile rộng 4b mà không vuông b x b: với tile b x b, mỗi lời gọi kernel liên tiếp
// (kk tăng) đọc b hàng MỚI của B, cách nhau 8n byte, nên mỗi lời gọi chạm b trang nhớ mới.
// Với tile rộng 4b, bốn lời gọi liền nhau (jj tăng) đọc tiếp cùng b hàng đó, giống thứ tự
// ii, kk, jj của bản tuần tự multiply_blocked. Đo sơ bộ trên máy Đức (n = 600, 1000, 2000;
// b = 48; tốt nhất 3 lần, máy nhiễu): tile b x 4b thường nhanh hơn tile b x b, nhiều lần đo
// chênh 20-40%, nhưng không phải cấu hình nào cũng vậy. Tile rộng hơn nữa (8b) hoặc vuông
// 4b x 4b không nhanh hơn rõ, mà số tile ít đi (khó chia đều).
// CHƯA CHỐT: chờ số liệu trên máy đo (scripts/experiments/tile_shape.conf, --variant simd-w1/w2/w4).
//
// Hai vòng ti (hàng tile), tj (cột tile) gộp bằng collapse(2) thành một danh sách tile,
// đánh số theo hàng:
//   schedule(static):  chia danh sách thành các đoạn liền nhau, mỗi thread một đoạn.
//                      Thực chất gần với dải hàng, không phải lưới p x p (lý thuyết mục 6.4).
//   schedule(dynamic): thread rảnh lấy tile tiếp theo (chunk 1), tự cân bằng tải khi tile biên
//                      nhỏ hơn, hoặc khi có thread chạy chậm hơn (lõi E, tiến trình nền).
// Số tile: n = 600, 1000, 2000 với b = 48 có 13 x 4, 21 x 6, 42 x 11 tile.
//
// Với mỗi C[i][j], các số hạng vẫn cộng theo k tăng dần như multiply_blocked (mm_kernel.hpp),
// nên kết quả giống từng bit bản tuần tự cùng b, với mọi số thread, mọi schedule, mọi bề rộng tile.

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "matmul/mm_kernel.hpp"

namespace mm {

enum class Schedule { Static, Dynamic };

inline const char* schedule_name(Schedule s) { return s == Schedule::Static ? "static" : "dynamic"; }

// Đổi tên lịch (--schedule) thành Schedule; tên sai thì ném std::invalid_argument.
inline Schedule parse_schedule(const std::string& name) {
    if (name == "static") return Schedule::Static;
    if (name == "dynamic") return Schedule::Dynamic;
    throw std::invalid_argument("--schedule phai la static hoac dynamic: " + name);
}

// Bề rộng tile tính bằng số block b (tile cao b, rộng kTileWidthBlocks * b).
constexpr int kTileWidthBlocks = 4;

// Biến thể của mm_par_2d (--variant): tên kernel, có thể kèm "-w<số>" để đổi bề rộng tile:
//   simd, novec            bề rộng mặc định kTileWidthBlocks
//   simd-w1, novec-w2, ... tile rộng 1, 2, ... block (1 đến 64); simd-w1 là tile vuông b x b
// Bề rộng nằm trong tên biến thể để ghi được vào cột variant của CSV mà không đổi định dạng CSV.
// Dùng cho thí nghiệm phụ scripts/experiments/tile_shape.conf (chọn bề rộng tile trên máy đo).
struct TileVariant {
    Kernel kernel = Kernel::Simd;
    int width_blocks = kTileWidthBlocks;
};

// Tên sai thì ném std::invalid_argument.
inline TileVariant parse_tile_variant(const std::string& name) {
    const std::string::size_type dash = name.find("-w");
    TileVariant v;
    v.kernel = parse_kernel(name.substr(0, dash));
    if (dash == std::string::npos) return v;
    const std::string digits = name.substr(dash + 2);
    const bool ok = !digits.empty() && digits.size() <= 2 &&
                    digits.find_first_not_of("0123456789") == std::string::npos && digits[0] != '0';
    if (!ok || std::stoi(digits) > 64) {
        throw std::invalid_argument("--variant: be rong tile phai la -w1 den -w64: " + name);
    }
    v.width_blocks = std::stoi(digits);
    return v;
}

namespace detail {

// Tính trọn tile hàng [i0, i1), cột [j0, j1) của C: gán 0 rồi cộng dồn qua mọi block kk.
inline void compute_tile(const double* A, const double* B, double* C, int n, int b, int i0, int i1, int j0, int j1,
                         Kernel kernel) {
    const std::size_t N = static_cast<std::size_t>(n);
    for (int i = i0; i < i1; ++i) std::fill(C + i * N + j0, C + i * N + j1, 0.0);
    for (int kk = 0; kk < n; kk += b) {
        const int k1 = std::min(kk + b, n);
        for (int jj = j0; jj < j1; jj += b) block_multiply(A, B, C, n, i0, i1, kk, k1, jj, std::min(jj + b, j1), kernel);
    }
}

// Số phần ceil(n / s), viết thế này để tránh tràn int ở n + s - 1.
inline int ceil_div(int n, int s) { return n / s + (n % s != 0); }

}  // namespace detail

// C = A * B song song theo tile b x (width_blocks * b), dùng đội thread OpenMP hiện tại
// (omp_set_num_threads hoặc pc::pin_omp_threads đặt trước khi gọi).
// Ghi đè C, không cần gán 0 trước. C không được chồng lên A hay B.
// width_blocks chỉ đổi để thử nghiệm và kiểm thử; chương trình dùng mặc định kTileWidthBlocks.
// Ném std::invalid_argument nếu n < 0, b < 1 hoặc width_blocks < 1.
inline void multiply_tiles(const double* A, const double* B, double* C, int n, int b,
                           Schedule schedule = Schedule::Static, Kernel kernel = Kernel::Simd,
                           int width_blocks = kTileWidthBlocks) {
    if (n < 0 || b < 1 || width_blocks < 1) {
        throw std::invalid_argument("multiply_tiles: can n >= 0, b >= 1, width_blocks >= 1");
    }
    const int w = b > n / width_blocks ? n : b * width_blocks;  // bề rộng tile (cột), không tràn int
    const int rows = detail::ceil_div(n, b);                     // số hàng tile
    const int cols = n == 0 ? 0 : detail::ceil_div(n, w);        // số cột tile

    // Hai vòng chỉ khác mệnh đề schedule: schedule(runtime) sẽ phụ thuộc biến OMP_SCHEDULE.
    if (schedule == Schedule::Static) {
#pragma omp parallel for collapse(2) schedule(static) default(none) shared(A, B, C, n, b, w, rows, cols, kernel)
        for (int ti = 0; ti < rows; ++ti)
            for (int tj = 0; tj < cols; ++tj)
                detail::compute_tile(A, B, C, n, b, ti * b, std::min(ti * b + b, n), tj * w, std::min(tj * w + w, n), kernel);
    } else {
#pragma omp parallel for collapse(2) schedule(dynamic, 1) default(none) shared(A, B, C, n, b, w, rows, cols, kernel)
        for (int ti = 0; ti < rows; ++ti)
            for (int tj = 0; tj < cols; ++tj)
                detail::compute_tile(A, B, C, n, b, ti * b, std::min(ti * b + b, n), tj * w, std::min(tj * w + w, n), kernel);
    }
}

}  // namespace mm
