#pragma once

// Vi nhân cho một block, dùng chung cho mm_block_seq, mm_par_1d, mm_par_2d, mm_cannon.
//
// Ma trận vuông n x n lưu liên tiếp theo hàng (row-major), như lớp Matrix:
// phần tử (i, j) nằm ở M[i * n + j]. Truyền thẳng Matrix::data() và n.
//
// Bản hiện tại là bản đơn giản (việc D2): thứ tự ikj trong block, chưa có
// __restrict__ và omp simd. Bản tối ưu là việc D4; chữ ký giữ nguyên.

#include <algorithm>
#include <cstddef>

namespace mm {

// Cộng dồn tích của phần A[i0:i1, k0:k1] và B[k0:k1, j0:j1] vào C[i0:i1, j0:j1]:
//     C[i][j] += sum_{k0 <= k < k1} A[i][k] * B[k][j]     với i0 <= i < i1, j0 <= j < j1
// Mọi khoảng là nửa mở [đầu, cuối). Khoảng rỗng thì không làm gì.
// Chỉ ghi vào C[i0:i1, j0:j1], không đụng phần còn lại của C, nên hai lời gọi
// có vùng (i, j) không giao nhau chạy song song được mà không tranh chấp ghi.
// Không tự gán 0 cho C: người gọi phải gán trước khi cộng dồn lần đầu.
//
// Thứ tự ikj: vòng trong cùng chạy theo j, đọc một hàng của B và ghi một hàng của C
// liền nhau trong bộ nhớ (tài liệu lý thuyết, mục 5.2); A[i][k] cố định trong vòng đó.
inline void block_multiply(const double* A, const double* B, double* C, int n,
                           int i0, int i1, int k0, int k1, int j0, int j1) {
    const std::size_t N = static_cast<std::size_t>(n);  // tránh tràn int khi tính i * n
    for (int i = i0; i < i1; ++i) {
        double* c_row = C + i * N;
        const double* a_row = A + i * N;
        for (int k = k0; k < k1; ++k) {
            const double a = a_row[k];
            const double* b_row = B + k * N;
            for (int j = j0; j < j1; ++j) c_row[j] += a * b_row[j];
        }
    }
}

// Nhân tuần tự theo block: C = A * B, block cỡ b x b.
// Gán 0 cho C rồi duyệt ba vòng block ii, kk, jj; block ở biên nhỏ hơn khi n không chia hết
// cho b (dùng min(ii + b, n), tài liệu lý thuyết mục 2.3). b >= n thì chỉ có một block.
inline void multiply_blocked(const double* A, const double* B, double* C, int n, int b) {
    std::fill(C, C + static_cast<std::size_t>(n) * n, 0.0);
    for (int ii = 0; ii < n; ii += b) {
        const int i1 = std::min(ii + b, n);
        for (int kk = 0; kk < n; kk += b) {
            const int k1 = std::min(kk + b, n);
            for (int jj = 0; jj < n; jj += b) {
                const int j1 = std::min(jj + b, n);
                block_multiply(A, B, C, n, ii, i1, kk, k1, jj, j1);
            }
        }
    }
}

}  // namespace mm
