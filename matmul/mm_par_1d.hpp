#pragma once

// Nhân ma trận song song OpenMP, phân hoạch 1D theo dải hàng (việc V9).
// Dùng chung cho matmul/mm_par_1d.cpp và tests/.
//
// Phân hoạch (PCAM, tài liệu lý thuyết mục 6.3):
//   - Tác vụ nguyên thủy: tính một hàng C[i][:] = A[i][:] * B. Các hàng độc lập nhau.
//   - Gộp: các hàng liền nhau thành một dải; mỗi thread chỉ ghi các hàng C của dải mình,
//     nên không có race condition và không cần đồng bộ ngoài barrier cuối vùng song song.
//   - Truyền thông: mỗi thread đọc dải A của riêng mình nhưng đọc TOÀN BỘ B. Với p thread,
//     B được đọc p lần (từ L3 dùng chung hoặc RAM). Đây là chi phí chính của 1D so với 2D/Cannon.
//
// Hai cách giao dải cho thread (--schedule):
//   static:  đúng p dải liền nhau, thread t nhận hàng [t*n/p, (t+1)*n/p). Các dải chênh nhau
//            nhiều nhất 1 hàng, kể cả khi n không chia hết cho b. Không có chi phí lập lịch.
//   dynamic: dải cao b hàng; thread rảnh lấy dải tiếp theo (schedule(dynamic, 1)). Tự cân bằng
//            tải khi có thread chạy chậm hơn (SMT, tiến trình nền), đổi lại tốn chi phí lập lịch.
//
// Trong mỗi dải, duyệt block ii (b hàng), kk, jj và gọi mm::block_multiply (kernel D4 của Đức),
// cùng thứ tự với multiply_blocked. Với mỗi C[i][j], các số hạng cộng theo k tăng dần, gộp 4 bước
// k bắt đầu từ cùng các kk, nên kết quả giống từng bit mm_block_seq cùng b, với mọi số thread
// và cả hai lịch (biên của dải theo hàng không ảnh hưởng tới phép tính trên từng phần tử).
//
// Phần việc của một dải nằm trong hàm riêng detail::compute_rows, nhận mọi tham số theo giá trị.
// Thân vùng omp parallel chỉ tính biên dải rồi gọi hàm này, để vòng lặp nóng không đọc biến
// chung của vùng song song (xem docs/ket_qua_tile_shape.md mục 4: bản tile chạy 1 thread chậm
// hơn mm_block_seq).

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include <omp.h>

#include "matmul/mm_kernel.hpp"
#include "matmul/mm_par_2d.hpp"   // mm::Schedule, mm::parse_schedule (dùng chung với bản tile)

namespace mm {

namespace detail {

// Tính trọn các hàng [r0, r1) của C: gán 0 rồi cộng dồn theo block (ii từ r0, bước b).
inline void compute_rows(const double* A, const double* B, double* C, int n, int b, int r0, int r1,
                         Kernel kernel) {
    if (r0 >= r1) return;
    const std::size_t N = static_cast<std::size_t>(n);
    std::fill(C + r0 * N, C + r1 * N, 0.0);
    for (int ii = r0; ii < r1; ii += b) {
        const int i1 = std::min(ii + b, r1);
        for (int kk = 0; kk < n; kk += b) {
            const int k1 = std::min(kk + b, n);
            for (int jj = 0; jj < n; jj += b) block_multiply(A, B, C, n, ii, i1, kk, k1, jj, std::min(jj + b, n), kernel);
        }
    }
}

// Hàng đầu của dải thứ t khi chia n hàng thành p dải gần bằng nhau (tính bằng long long, không tràn).
inline int strip_begin(int n, int t, int p) {
    return static_cast<int>(static_cast<long long>(n) * t / p);
}

}  // namespace detail

// C = A * B song song theo dải hàng, dùng đội thread OpenMP hiện tại
// (omp_set_num_threads hoặc pc::pin_omp_threads đặt trước khi gọi).
// Ghi đè C, không cần gán 0 trước. C không được chồng lên A hay B.
// Ném std::invalid_argument nếu n < 0 hoặc b < 1.
inline void multiply_rows(const double* A, const double* B, double* C, int n, int b,
                          Schedule schedule = Schedule::Static, Kernel kernel = Kernel::Simd) {
    if (n < 0 || b < 1) throw std::invalid_argument("multiply_rows: can n >= 0, b >= 1");
    if (n == 0) return;

    if (schedule == Schedule::Static) {
#pragma omp parallel default(none) shared(A, B, C, n, b, kernel)
        {
            const int p = omp_get_num_threads();
            const int t = omp_get_thread_num();
            detail::compute_rows(A, B, C, n, b, detail::strip_begin(n, t, p), detail::strip_begin(n, t + 1, p),
                                 kernel);
        }
    } else {
        const int strips = n / b + (n % b != 0);   // ceil(n / b), không tràn int
#pragma omp parallel for schedule(dynamic, 1) default(none) shared(A, B, C, n, b, kernel, strips)
        for (int s = 0; s < strips; ++s) {
            const int r0 = s * b;   // s * b < n nên không tràn
            detail::compute_rows(A, B, C, n, b, r0, r0 + std::min(b, n - r0), kernel);
        }
    }
}

}  // namespace mm
