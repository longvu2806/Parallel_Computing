#pragma once

// Nhân ma trận song song theo thuật toán Cannon, mô phỏng trên bộ nhớ chia sẻ bằng OpenMP (việc D6).
// Dùng chung cho matmul/mm_cannon.cpp và tests/.
//
// Cannon (tài liệu lý thuyết mục 6.5) dành cho p = q x q bộ xử lý xếp thành lưới vòng. A, B, C chia
// thành q x q khối lớn; bộ xử lý (I, J) giữ khối C[I][J] và ở bước s = 0, 1, ..., q-1 tính
//     C[I][J] += A[I][m] * B[m][J],    m = (I + J + s) mod q
// Chỉ số m đã gộp bước căn chỉnh ban đầu (hàng I của A dịch trái I khối, cột J của B dịch lên J
// khối) và các lần dịch sau mỗi bước. Khi s chạy hết 0..q-1, m đi qua đủ q khối, nên đủ mọi số hạng.
//
// Trên bộ nhớ chia sẻ, A và B nằm sẵn trong RAM chung: "dịch khối" chỉ là đổi m, không chép dữ liệu.
// Mỗi thread chỉ ghi khối C của mình, A và B chỉ được đọc, nên KHÔNG có race condition kể cả khi
// không đồng bộ giữa các bước. Hai biến thể (--variant, cho thí nghiệm E7):
//   barrier:   #pragma omp barrier giữa hai bước liền nhau. Mô phỏng đúng Cannon trên hệ phân tán,
//              nơi không bộ xử lý nào sang bước s + 1 khi chưa nhận được khối hàng xóm dịch tới.
//              Ở mỗi bước, mỗi khối của A và của B được đúng một thread đọc. Mỗi barrier phải chờ
//              thread chậm nhất của bước đó.
//   nobarrier: bỏ các barrier đó, mỗi thread chạy liền q bước của mình (chỉ còn barrier ngầm cuối
//              vùng song song). Vẫn đúng vì lý do trên. Chênh lệch thời gian giữa hai biến thể là
//              chi phí đồng bộ, gồm cả thời gian chờ khi các thread chạy nhanh chậm khác nhau.
// Ưu điểm về bộ nhớ và truyền thông của Cannon chỉ thể hiện trên hệ phân tán; ở đây khác biệt so với
// tile hay 1D chỉ đến từ cách chia việc và thứ tự duyệt khối.
//
// Lưới: thread t giữ ô (I, J) = (t / q, t mod q), nên số thread phải là q * q (1, 4, 9, ...).
// Nếu đội thread không đúng q * q (chỉ gặp trong test), ô c giao cho thread c mod p, vẫn đúng và
// không kẹt barrier (mọi thread đi qua cùng số barrier).
// Khối lớn thứ I gồm hàng (hoặc cột) [I * n / q, (I + 1) * n / q): các khối chênh nhau nhiều nhất 1.
// Trong một bước, nhân khối lớn bằng cách chia block b x b như multiply_blocked (vòng ii, kk, jj gọi
// mm::block_multiply, kernel D4).
//
// Thứ tự cộng: với C[i][j] thuộc ô (I, J), các số hạng cộng theo k tăng dần, nhưng bắt đầu từ khối
// m = (I + J) mod q rồi vòng tròn, không bắt đầu từ k = 0 như bản tuần tự. Do đó:
//   - q = 1 (1 thread): giống từng bit multiply_blocked cùng b.
//   - q >= 2: có thể lệch bản tuần tự ở vài bit cuối (vẫn đạt ngưỡng của --verify); dữ liệu số
//     nguyên nhỏ thì khớp tuyệt đối.
//   - Cùng q và b: barrier, nobarrier và mọi cách giao ô cho thread cho kết quả giống từng bit.
//
// Phần việc của một bước nằm trong hàm riêng detail::cannon_step, nhận mọi tham số theo giá trị
// (như compute_rows của mm_par_1d; xem docs/ket_qua_tile_shape.md mục 4).

#include <omp.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "matmul/mm_kernel.hpp"

namespace mm {

enum class CannonSync { Barrier, NoBarrier };

inline const char* cannon_sync_name(CannonSync s) { return s == CannonSync::Barrier ? "barrier" : "nobarrier"; }

// Đổi tên biến thể (--variant) thành CannonSync; tên sai thì ném std::invalid_argument.
inline CannonSync parse_cannon_sync(const std::string& name) {
    if (name == "barrier") return CannonSync::Barrier;
    if (name == "nobarrier") return CannonSync::NoBarrier;
    throw std::invalid_argument("--variant cua mm_cannon phai la barrier hoac nobarrier: " + name);
}

// Cạnh q của lưới q x q cho p thread. p không phải số chính phương thì ném std::invalid_argument.
inline int cannon_grid_side(int p) {
    int q = 0;
    while (static_cast<long long>(q + 1) * (q + 1) <= p) ++q;
    if (p < 1 || static_cast<long long>(q) * q != p) {
        throw std::invalid_argument("Cannon can so thread chinh phuong (1, 4, 9, ...), nhan " + std::to_string(p));
    }
    return q;
}

// Khối k mà ô (I, J) dùng ở bước s: nhân A[I][m] với B[m][J], m = (I + J + s) mod q.
// Tính chất của Cannon (tests/test_block_par.cpp kiểm tra): với mỗi ô, m đi qua đủ 0..q-1; ở mỗi
// bước, các ô cùng hàng I dùng q khối A khác nhau và các ô cùng cột J dùng q khối B khác nhau.
inline int cannon_k_block(int I, int J, int s, int q) { return (I + J + s) % q; }

namespace detail {

// Chỉ số đầu của khối lớn thứ I khi chia n thành q khối gần bằng nhau (long long, không tràn).
inline int cannon_begin(int n, int I, int q) { return static_cast<int>(static_cast<long long>(n) * I / q); }

// Gán 0 cho vùng C[r0:r1, c0:c1].
inline void zero_block(double* C, int n, int r0, int r1, int c0, int c1) {
    const std::size_t N = static_cast<std::size_t>(n);
    for (int i = r0; i < r1; ++i) std::fill(C + i * N + c0, C + i * N + c1, 0.0);
}

// Một bước Cannon: C[r0:r1, c0:c1] += A[r0:r1, k0:k1] * B[k0:k1, c0:c1], chia block b x b
// theo thứ tự ii, kk, jj như multiply_blocked.
inline void cannon_step(const double* A, const double* B, double* C, int n, int b, int r0, int r1, int k0, int k1,
                        int c0, int c1) {
    for (int ii = r0; ii < r1; ii += b) {
        const int i1 = std::min(ii + b, r1);
        for (int kk = k0; kk < k1; kk += b) {
            const int kend = std::min(kk + b, k1);
            for (int jj = c0; jj < c1; jj += b) block_multiply(A, B, C, n, ii, i1, kk, kend, jj, std::min(jj + b, c1));
        }
    }
}

}  // namespace detail

// C = A * B theo Cannon trên lưới q x q, dùng đội thread OpenMP hiện tại
// (omp_set_num_threads hoặc pc::pin_omp_threads đặt trước khi gọi; nên đúng q * q thread).
// Ghi đè C, không cần gán 0 trước. C không được chồng lên A hay B.
// Ném std::invalid_argument nếu n < 0, b < 1 hoặc q < 1.
inline void multiply_cannon(const double* A, const double* B, double* C, int n, int b, int q,
                            CannonSync sync = CannonSync::Barrier) {
    if (n < 0 || b < 1 || q < 1) throw std::invalid_argument("multiply_cannon: can n >= 0, b >= 1, q >= 1");
    if (n == 0) return;
    const bool use_barrier = sync == CannonSync::Barrier;

#pragma omp parallel default(none) shared(A, B, C, n, b, q, use_barrier)
    {
        const int p = omp_get_num_threads();
        const int t = omp_get_thread_num();
        const int cells = q * q;

        // Mỗi thread gán 0 khối C của mình (không cần barrier: chỉ chính thread đó ghi khối này).
        for (int c = t; c < cells; c += p) {
            const int I = c / q, J = c % q;
            detail::zero_block(C, n, detail::cannon_begin(n, I, q), detail::cannon_begin(n, I + 1, q),
                               detail::cannon_begin(n, J, q), detail::cannon_begin(n, J + 1, q));
        }

        for (int s = 0; s < q; ++s) {
            for (int c = t; c < cells; c += p) {
                const int I = c / q, J = c % q;
                const int m = cannon_k_block(I, J, s, q);
                detail::cannon_step(A, B, C, n, b, detail::cannon_begin(n, I, q), detail::cannon_begin(n, I + 1, q),
                                    detail::cannon_begin(n, m, q), detail::cannon_begin(n, m + 1, q),
                                    detail::cannon_begin(n, J, q), detail::cannon_begin(n, J + 1, q));
            }
            // Barrier giữa hai bước; sau bước cuối đã có barrier ngầm của vùng song song.
            if (use_barrier && s + 1 < q) {
#pragma omp barrier
            }
        }
    }
}

}  // namespace mm
