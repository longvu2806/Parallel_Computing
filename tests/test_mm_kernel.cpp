// Kiểm thử matmul/mm_kernel.hpp: block_multiply và multiply_blocked, cả hai biến thể kernel
// (Kernel::Simd và Kernel::NoVec). So với phép nhân ba vòng lặp đơn giản viết riêng trong file này.
// Dữ liệu số nguyên nhỏ (Fill::SmallInt) nên kết quả phải khớp từng bit, so bằng ==.
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include "common/matrix.hpp"
#include "matmul/mm_kernel.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

// C = A * B theo định nghĩa, thứ tự ijk. Chỉ dùng làm mốc so sánh.
Matrix naive(const Matrix& A, const Matrix& B) {
    const std::size_t n = A.rows();
    Matrix C(n, n);
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            double s = 0.0;
            for (std::size_t k = 0; k < n; ++k) s += A(i, k) * B(k, j);
            C(i, j) = s;
        }
    return C;
}

bool same(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k)
        if (a[k] != b[k]) return false;
    return true;
}

double max_abs_diff(const Matrix& a, const Matrix& b) {
    double d = 0.0;
    for (std::size_t k = 0; k < a.size(); ++k) d = std::fmax(d, std::fabs(a[k] - b[k]));
    return d;
}

void make_input(int n, unsigned seed, Matrix& A, Matrix& B, Fill kind = Fill::SmallInt) {
    generate_matmul_input(static_cast<std::size_t>(n), seed, A, B, kind);
}

// Các kiểm tra 1-6 chạy cho từng biến thể kernel; tên kiểm tra có tiền tố [simd] hoặc [novec].
void test_kernel(mm::Kernel kern) {
    const std::string tag = std::string("[") + mm::kernel_name(kern) + "] ";

    // 1. Một lời gọi block_multiply phủ cả ma trận chính là phép nhân đầy đủ.
    {
        const int n = 13;
        Matrix A, B;
        make_input(n, 1, A, B);
        Matrix C(n, n);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 0, n, 0, n, 0, n, kern);
        check(same(C, naive(A, B)), tag + "block_multiply tren ca ma tran 13x13 khop ban ijk");
    }

    // 2. block_multiply cộng dồn, không ghi đè: gọi hai lần được 2 * A * B.
    {
        const int n = 9;
        Matrix A, B;
        make_input(n, 2, A, B);
        Matrix C(n, n);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 0, n, 0, n, 0, n, kern);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 0, n, 0, n, 0, n, kern);
        const Matrix ref = naive(A, B);
        bool ok = true;
        for (std::size_t k = 0; k < C.size(); ++k) ok = ok && C[k] == 2.0 * ref[k];
        check(ok, tag + "goi hai lan thi cong don thanh 2 * A * B");
    }

    // 3. Chỉ ghi vào C[i0:i1, j0:j1], chỉ cộng các k trong [k0, k1).
    {
        const int n = 12;
        const int i0 = 3, i1 = 8, k0 = 2, k1 = 7, j0 = 5, j1 = 11;
        Matrix A, B;
        make_input(n, 3, A, B);
        Matrix C(n, n);
        C.fill(-99.0);  // giá trị canh: phần ngoài vùng phải giữ nguyên
        mm::block_multiply(A.data(), B.data(), C.data(), n, i0, i1, k0, k1, j0, j1, kern);
        bool inside = true, outside = true;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const bool in = i >= i0 && i < i1 && j >= j0 && j < j1;
                double expect = -99.0;
                if (in)
                    for (int k = k0; k < k1; ++k) expect += A(i, k) * B(k, j);
                if (in) inside = inside && C(i, j) == expect;
                else outside = outside && C(i, j) == -99.0;
            }
        check(inside, tag + "vung [3,8)x[5,11) cong dung tong theo k trong [2,7)");
        check(outside, tag + "ngoai vung khong bi ghi");
    }

    // 4. Khoảng rỗng không làm gì.
    {
        const int n = 6;
        Matrix A, B;
        make_input(n, 4, A, B);
        Matrix C(n, n);
        C.fill(7.0);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 2, 2, 0, n, 0, n, kern);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 0, n, 3, 3, 0, n, kern);
        mm::block_multiply(A.data(), B.data(), C.data(), n, 0, n, 0, n, 4, 4, kern);
        bool ok = true;
        for (std::size_t k = 0; k < C.size(); ++k) ok = ok && C[k] == 7.0;
        check(ok, tag + "khoang rong (i0 = i1, k0 = k1 hoac j0 = j1) khong doi C");
    }

    // 5. multiply_blocked khớp bản ijk với nhiều cặp (n, b), kể cả n không chia hết cho b,
    //    b = 1, b = n và b > n.
    {
        const int cases[][2] = {{1, 1},  {2, 1},  {7, 3},  {16, 4}, {17, 4},  {31, 8},
                                {33, 8}, {64, 64}, {65, 64}, {10, 100}, {100, 7}, {128, 32}};
        for (const auto& c : cases) {
            const int n = c[0], b = c[1];
            Matrix A, B;
            make_input(n, 100 + n, A, B);
            Matrix C(n, n);
            C.fill(123.0);  // multiply_blocked phải tự gán 0
            mm::multiply_blocked(A.data(), B.data(), C.data(), n, b, kern);
            check(same(C, naive(A, B)),
                  tag + "multiply_blocked n=" + std::to_string(n) + " b=" + std::to_string(b) + " khop ban ijk");
        }
    }

    // 6. Số thực: lệch so với bản ijk chỉ ở mức làm tròn.
    //    Ngưỡng theo mục 2.4 tài liệu lý thuyết: |sai số| <= gamma_n * sum|a*b|, mỗi |a*b| < 1.
    {
        const int n = 200, b = 48;
        Matrix A, B;
        make_input(n, 7, A, B, Fill::Uniform);
        Matrix C(n, n);
        mm::multiply_blocked(A.data(), B.data(), C.data(), n, b, kern);
        const double u = 1.1102230246251565e-16;  // 2^-53
        const double bound = 2.0 * n * u * n;      // gap doi gamma_n * n
        const double d = max_abs_diff(C, naive(A, B));
        char buf[96];
        std::snprintf(buf, sizeof buf, "so thuc n=200 b=48: lech lon nhat %.3g <= %.3g", d, bound);
        check(d <= bound, tag + buf);
    }

}

}  // namespace

int main() {
    test_kernel(mm::Kernel::Simd);
    test_kernel(mm::Kernel::NoVec);

    // 7. Hai biến thể giống nhau từng bit kể cả với số thực: vector hóa vòng j không đổi
    //    thứ tự cộng (mỗi C[i][j] vẫn cộng các tích theo k tăng dần). Nhiều cặp (n, b) để
    //    gặp đủ trường hợp: số bước k chia hết hoặc không chia hết cho 4, độ dài j lẻ.
    {
        const int cases[][2] = {{97, 32}, {100, 48}, {130, 64}, {200, 13}};
        for (const auto& c : cases) {
            const int n = c[0], b = c[1];
            Matrix A, B;
            make_input(n, 300 + n, A, B, Fill::Uniform);
            Matrix Cs(n, n), Cn(n, n);
            mm::multiply_blocked(A.data(), B.data(), Cs.data(), n, b, mm::Kernel::Simd);
            mm::multiply_blocked(A.data(), B.data(), Cn.data(), n, b, mm::Kernel::NoVec);
            check(same(Cs, Cn), "so thuc n=" + std::to_string(n) + " b=" + std::to_string(b) +
                                    ": simd va novec giong nhau tung bit");
        }
    }

    // 8. Mặc định là Kernel::Simd; tên biến thể đổi qua lại đúng, tên sai bị từ chối.
    {
        const int n = 21;
        Matrix A, B;
        make_input(n, 8, A, B, Fill::Uniform);
        Matrix C1(n, n), C2(n, n);
        mm::multiply_blocked(A.data(), B.data(), C1.data(), n, 8);
        mm::multiply_blocked(A.data(), B.data(), C2.data(), n, 8, mm::Kernel::Simd);
        check(same(C1, C2), "multiply_blocked mac dinh dung Kernel::Simd");
        check(mm::parse_kernel("simd") == mm::Kernel::Simd && mm::parse_kernel("novec") == mm::Kernel::NoVec,
              "parse_kernel nhan simd va novec");
        check(std::string(mm::kernel_name(mm::parse_kernel("novec"))) == "novec", "kernel_name nguoc voi parse_kernel");
        bool threw = false;
        try {
            mm::parse_kernel("ikj");
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "parse_kernel tu choi ten sai (ikj)");
    }

    if (g_failures == 0) {
        std::printf("test_mm_kernel: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_mm_kernel: %d kiem tra that bai\n", g_failures);
    return 1;
}
