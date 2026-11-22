#pragma once

// Vi nhân cho một block, dùng chung cho mm_block_seq, mm_par_1d, mm_par_2d, mm_cannon.
//
// Ma trận vuông n x n lưu liên tiếp theo hàng (row-major), như lớp Matrix:
// phần tử (i, j) nằm ở M[i * n + j]. Truyền thẳng Matrix::data() và n.
//
// Bản tối ưu (việc D4), so với bản đơn giản của D2 (cùng thứ tự ikj, cùng chữ ký):
//   1. __restrict__: hứa A, B, C không chồng nhau. Không có nó, GCC phải sinh hai bản
//      vòng lặp và kiểm tra địa chỉ lúc chạy ("loop versioned for vectorization because
//      of possible aliasing"), vì ghi C[i][j] có thể làm đổi B[k][j].
//   2. #pragma omp simd trên vòng j: yêu cầu vector hóa (AVX2: 4 double mỗi lệnh FMA).
//      Vòng j không có phụ thuộc giữa các lần lặp (mỗi j ghi một C[i][j] riêng),
//      nên vector hóa không đổi thứ tự cộng và không đổi kết quả.
//   3. Gộp 4 bước k trong một lần quét j: đọc C[i][j] vào thanh ghi một lần, cộng 4 tích
//      A[i][k..k+3] * B[k..k+3][j], rồi ghi lại một lần. Bản đơn giản đọc và ghi C[i][j]
//      cho mỗi tích, nên bị giới hạn bởi số lệnh load/store chứ không phải số FMA.
//      Bốn tích vẫn cộng lần lượt theo k tăng dần, nên kết quả giống hệt bản đơn giản.
//
// Đo trên một P-core máy Đức (i5-12450H), n = 1000, b = 64: bản đơn giản 20 GFLOP/s,
// thêm (1)+(2) 27 GFLOP/s, thêm (3) 36 GFLOP/s; tắt vector hóa (Kernel::NoVec) 8 GFLOP/s.
//
// Kernel::NoVec là cùng mã nhưng cấm vector hóa (#pragma GCC novector, GCC >= 14),
// dùng cho thí nghiệm E4 "vector hóa đóng góp bao nhiêu".

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace mm {

// Biến thể kernel, chọn bằng --variant: simd (mặc định) hoặc novec.
enum class Kernel { Simd, NoVec };

inline const char* kernel_name(Kernel k) { return k == Kernel::Simd ? "simd" : "novec"; }

// Đổi tên biến thể thành Kernel; tên sai thì ném std::invalid_argument.
inline Kernel parse_kernel(const std::string& name) {
    if (name == "simd") return Kernel::Simd;
    if (name == "novec") return Kernel::NoVec;
    throw std::invalid_argument("--variant phai la simd hoac novec: " + name);
}

namespace detail {

// c[j] += a0*b0[j] + a1*b1[j] + a2*b2[j] + a3*b3[j] với j0 <= j < j1, cộng lần lượt từ trái sang.
template <Kernel K>
inline void row_update4(double* __restrict__ c, const double* __restrict__ b0, const double* __restrict__ b1,
                        const double* __restrict__ b2, const double* __restrict__ b3, double a0, double a1,
                        double a2, double a3, int j0, int j1) {
    if constexpr (K == Kernel::Simd) {
#pragma omp simd
        for (int j = j0; j < j1; ++j) {
            double s = c[j];
            s += a0 * b0[j];
            s += a1 * b1[j];
            s += a2 * b2[j];
            s += a3 * b3[j];
            c[j] = s;
        }
    } else {
#pragma GCC novector
        for (int j = j0; j < j1; ++j) {
            double s = c[j];
            s += a0 * b0[j];
            s += a1 * b1[j];
            s += a2 * b2[j];
            s += a3 * b3[j];
            c[j] = s;
        }
    }
}

// c[j] += a * b[j] với j0 <= j < j1 (phần k lẻ còn lại khi k1 - k0 không chia hết cho 4).
template <Kernel K>
inline void row_update1(double* __restrict__ c, const double* __restrict__ b, double a, int j0, int j1) {
    if constexpr (K == Kernel::Simd) {
#pragma omp simd
        for (int j = j0; j < j1; ++j) c[j] += a * b[j];
    } else {
#pragma GCC novector
        for (int j = j0; j < j1; ++j) c[j] += a * b[j];
    }
}

template <Kernel K>
inline void block_multiply_impl(const double* __restrict__ A, const double* __restrict__ B, double* __restrict__ C,
                                int n, int i0, int i1, int k0, int k1, int j0, int j1) {
    const std::size_t N = static_cast<std::size_t>(n);  // tránh tràn int khi tính i * n
    for (int i = i0; i < i1; ++i) {
        double* c_row = C + i * N;
        const double* a_row = A + i * N;
        int k = k0;
        for (; k + 4 <= k1; k += 4) {
            const double* b_row = B + k * N;
            row_update4<K>(c_row, b_row, b_row + N, b_row + 2 * N, b_row + 3 * N, a_row[k], a_row[k + 1],
                           a_row[k + 2], a_row[k + 3], j0, j1);
        }
        for (; k < k1; ++k) row_update1<K>(c_row, B + k * N, a_row[k], j0, j1);
    }
}

}  // namespace detail

// Cộng dồn tích của phần A[i0:i1, k0:k1] và B[k0:k1, j0:j1] vào C[i0:i1, j0:j1]:
//     C[i][j] += sum_{k0 <= k < k1} A[i][k] * B[k][j]     với i0 <= i < i1, j0 <= j < j1
// Mọi khoảng là nửa mở [đầu, cuối). Khoảng rỗng thì không làm gì.
// Chỉ ghi vào C[i0:i1, j0:j1], không đụng phần còn lại của C, nên hai lời gọi
// có vùng (i, j) không giao nhau chạy song song được mà không tranh chấp ghi.
// Không tự gán 0 cho C: người gọi phải gán trước khi cộng dồn lần đầu.
// C không được chồng lên A hay B (__restrict__); A và B được phép là cùng một ma trận.
//
// Thứ tự ikj: vòng trong cùng chạy theo j, đọc hàng của B và ghi một hàng của C
// liền nhau trong bộ nhớ (tài liệu lý thuyết, mục 5.2); A[i][k] cố định trong vòng đó.
inline void block_multiply(const double* A, const double* B, double* C, int n, int i0, int i1, int k0, int k1,
                           int j0, int j1, Kernel kernel = Kernel::Simd) {
    if (kernel == Kernel::Simd)
        detail::block_multiply_impl<Kernel::Simd>(A, B, C, n, i0, i1, k0, k1, j0, j1);
    else
        detail::block_multiply_impl<Kernel::NoVec>(A, B, C, n, i0, i1, k0, k1, j0, j1);
}

// Nhân tuần tự theo block: C = A * B, block cỡ b x b.
// Gán 0 cho C rồi duyệt ba vòng block ii, kk, jj; block ở biên nhỏ hơn khi n không chia hết
// cho b (dùng min(ii + b, n), tài liệu lý thuyết mục 2.3). b >= n thì chỉ có một block.
inline void multiply_blocked(const double* A, const double* B, double* C, int n, int b,
                             Kernel kernel = Kernel::Simd) {
    std::fill(C, C + static_cast<std::size_t>(n) * n, 0.0);
    for (int ii = 0; ii < n; ii += b) {
        const int i1 = std::min(ii + b, n);
        for (int kk = 0; kk < n; kk += b) {
            const int k1 = std::min(kk + b, n);
            for (int jj = 0; jj < n; jj += b) {
                const int j1 = std::min(jj + b, n);
                block_multiply(A, B, C, n, ii, i1, kk, k1, jj, j1, kernel);
            }
        }
    }
}

}  // namespace mm
