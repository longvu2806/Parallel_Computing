#pragma once

// Nhân ma trận vuông C = A * B theo 6 thứ tự vòng lặp, không chia block.
// Ma trận lưu theo hàng: X[i][j] = X[i * n + j]. Dùng chung cho matmul/mm_seq.cpp và tests/.
//
// Vòng trong cùng quyết định cách truy cập bộ nhớ:
//   ijk, jik: vòng trong chạy theo k -> đọc A theo hàng (liên tiếp), B theo cột (bước n)
//   ikj, kij: vòng trong chạy theo j -> đọc B và ghi C theo hàng (liên tiếp)    : nhanh nhất
//   jki, kji: vòng trong chạy theo i -> đọc A và ghi C theo cột (bước n)          : chậm nhất
//
// Cả 6 thứ tự đều cộng các số hạng A[i][k] * B[k][j] vào C[i][j] theo k tăng dần. Với -O0 chúng
// cho cùng kết quả từng bit. Với -O3 -march=native, trình biên dịch có thể gộp nhân-cộng thành
// FMA ở vòng này mà không ở vòng kia: trên GCC 16 đã thấy {ijk, jik} và {ikj, kij, jki, kji}
// lệch nhau vài ulp. Cả hai nhóm đều nằm trong ngưỡng sai số của common/verify.

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace mm {

enum class LoopOrder { IJK, IKJ, JIK, JKI, KIJ, KJI };

constexpr LoopOrder kAllLoopOrders[] = {LoopOrder::IJK, LoopOrder::IKJ, LoopOrder::JIK,
                                        LoopOrder::JKI, LoopOrder::KIJ, LoopOrder::KJI};

inline const char* loop_order_name(LoopOrder o) {
    switch (o) {
        case LoopOrder::IJK: return "ijk";
        case LoopOrder::IKJ: return "ikj";
        case LoopOrder::JIK: return "jik";
        case LoopOrder::JKI: return "jki";
        case LoopOrder::KIJ: return "kij";
        case LoopOrder::KJI: return "kji";
    }
    return "?";
}

// Đổi tên biến thể (--variant) thành LoopOrder; tên sai thì ném std::invalid_argument.
inline LoopOrder parse_loop_order(const std::string& name) {
    for (LoopOrder o : kAllLoopOrders) {
        if (name == loop_order_name(o)) return o;
    }
    throw std::invalid_argument("--variant phai la mot trong ijk, ikj, jik, jki, kij, kji: " + name);
}

// Mọi hàm dưới đây ghi đè C bằng A * B (không cần gán 0 trước).
// __restrict: A, B, C không chồng nhau, để trình biên dịch tự vector hóa vòng trong.

// C[i][j] tính xong trong một biến tạm rồi mới ghi.
inline void multiply_ijk(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < n; ++k) s += A[i * n + k] * B[k * n + j];
            C[i * n + j] = s;
        }
    }
}

inline void multiply_jik(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t i = 0; i < n; ++i) {
            double s = 0.0;
            for (std::size_t k = 0; k < n; ++k) s += A[i * n + k] * B[k * n + j];
            C[i * n + j] = s;
        }
    }
}

// Bốn thứ tự còn lại cộng dồn vào C nên phải gán 0 trước (O(n^2), không đáng kể so với O(n^3)).
inline void multiply_ikj(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    std::fill_n(C, n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            const double a = A[i * n + k];
            const double* b = B + k * n;
            double* c = C + i * n;
            for (std::size_t j = 0; j < n; ++j) c[j] += a * b[j];
        }
    }
}

inline void multiply_kij(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    std::fill_n(C, n * n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t i = 0; i < n; ++i) {
            const double a = A[i * n + k];
            const double* b = B + k * n;
            double* c = C + i * n;
            for (std::size_t j = 0; j < n; ++j) c[j] += a * b[j];
        }
    }
}

inline void multiply_jki(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    std::fill_n(C, n * n, 0.0);
    for (std::size_t j = 0; j < n; ++j) {
        for (std::size_t k = 0; k < n; ++k) {
            const double b = B[k * n + j];
            for (std::size_t i = 0; i < n; ++i) C[i * n + j] += A[i * n + k] * b;
        }
    }
}

inline void multiply_kji(const double* __restrict A, const double* __restrict B, double* __restrict C,
                         std::size_t n) {
    std::fill_n(C, n * n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t j = 0; j < n; ++j) {
            const double b = B[k * n + j];
            for (std::size_t i = 0; i < n; ++i) C[i * n + j] += A[i * n + k] * b;
        }
    }
}

inline void multiply(LoopOrder o, const double* A, const double* B, double* C, std::size_t n) {
    switch (o) {
        case LoopOrder::IJK: multiply_ijk(A, B, C, n); return;
        case LoopOrder::IKJ: multiply_ikj(A, B, C, n); return;
        case LoopOrder::JIK: multiply_jik(A, B, C, n); return;
        case LoopOrder::JKI: multiply_jki(A, B, C, n); return;
        case LoopOrder::KIJ: multiply_kij(A, B, C, n); return;
        case LoopOrder::KJI: multiply_kji(A, B, C, n); return;
    }
}

}  // namespace mm
