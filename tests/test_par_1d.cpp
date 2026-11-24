// Kiểm thử nhân ma trận song song 1D theo dải hàng (matmul/mm_par_1d.hpp, việc V9):
//   - chia dải: các dải phủ đủ n hàng, không chồng nhau, chênh nhau nhiều nhất 1 hàng;
//   - SmallInt: 1-12 thread, cả hai lịch, khớp tuyệt đối với bản tham chiếu (common/verify);
//   - số thực: giống từng bit bản tuần tự mm::multiply_blocked cùng b, cả hai kernel.
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include <omp.h>

#include <cmath>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>

#include "common/matrix.hpp"
#include "common/verify.hpp"
#include "matmul/mm_kernel.hpp"
#include "matmul/mm_par_1d.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

bool identical(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (!(a[k] == b[k])) return false;   // NaN (ô chưa được ghi) luôn bị coi là khác
    }
    return true;
}

constexpr int kMaxThreads = 12;  // số CPU logic của máy đo
constexpr mm::Schedule kSchedules[] = {mm::Schedule::Static, mm::Schedule::Dynamic};

void set_threads(int t) {
    omp_set_dynamic(0);
    omp_set_num_threads(t);
}

// C = A * B bằng multiply_rows; C ban đầu chứa NaN để chắc chắn mọi ô đều được ghi đè.
Matrix rows(const Matrix& A, const Matrix& B, int b, mm::Schedule s, mm::Kernel k = mm::Kernel::Simd) {
    Matrix C(A.rows(), A.rows());
    C.fill(NAN);
    mm::multiply_rows(A.data(), B.data(), C.data(), static_cast<int>(A.rows()), b, s, k);
    return C;
}

std::string where(int t, mm::Schedule s) { return " " + std::to_string(t) + "/" + mm::schedule_name(s); }

// ---------------------------------------------------------------- chia dải và tham số sai

void test_api() {
    std::printf("-- chia dai, tham so sai\n");
    // Với mọi n, p: dải t là [strip_begin(t), strip_begin(t+1)), phủ đủ [0, n), độ dài floor hoặc ceil(n/p).
    bool ok = true;
    for (int n : {0, 1, 5, 12, 13, 100, 1000, 2000}) {
        for (int p = 1; p <= 16; ++p) {
            ok = ok && mm::detail::strip_begin(n, 0, p) == 0 && mm::detail::strip_begin(n, p, p) == n;
            for (int t = 0; t < p; ++t) {
                const int len = mm::detail::strip_begin(n, t + 1, p) - mm::detail::strip_begin(n, t, p);
                ok = ok && len >= n / p && len <= n / p + (n % p != 0);
            }
        }
    }
    check(ok, "static: p dai phu du n hang, khong chong nhau, chenh nhau nhieu nhat 1 hang");
    check(mm::detail::strip_begin(2000000000, 11, 12) == 1833333333, "strip_begin khong tran int voi n lon");

    double c = 1.0;
    bool threw = false;
    try { mm::multiply_rows(&c, &c, &c, 1, 0); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "b = 0 bao loi");
    threw = false;
    try { mm::multiply_rows(&c, &c, &c, -1, 8); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "n am bao loi");

    c = 5.0;
    mm::multiply_rows(nullptr, nullptr, &c, 0, 8);
    check(c == 5.0, "n = 0: khong lam gi");
}

// ---------------------------------------------------------------- SmallInt, khớp tuyệt đối

// Gồm: n < số thread (n = 5: tới 7 thread không có hàng nào ở lịch static), n = 1, b = 1,
// b > n, n không chia hết cho b, và n chia hết cho cả b lẫn số thread.
void test_exact() {
    std::printf("-- SmallInt, 1-%d thread, khop tuyet doi voi tham chieu\n", kMaxThreads);
    const int cases[][2] = {{1, 1}, {5, 8}, {7, 1}, {37, 8}, {64, 16}, {100, 48}, {130, 48}, {50, 64}, {144, 12}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 700 + n, A, B, Fill::SmallInt);
        const Matrix R = mm::reference_multiply(A, B);
        std::string failed;
        for (int t = 1; t <= kMaxThreads; ++t) {
            set_threads(t);
            for (mm::Schedule s : kSchedules) {
                if (!identical(rows(A, B, b, s), R)) failed += where(t, s);
            }
        }
        check(failed.empty(), "n = " + std::to_string(n) + ", b = " + std::to_string(b) +
                                  ": moi so thread, ca hai lich khop tham chieu" +
                                  (failed.empty() ? "" : ", sai o (thread/lich):" + failed));
    }
}

// ---------------------------------------------------------------- số thực

// Kết quả phải giống từng bit multiply_blocked cùng b (cùng thứ tự cộng theo k), với mọi số thread,
// mọi lịch, cả hai kernel; và đạt ngưỡng sai số so với tham chiếu.
void test_uniform() {
    std::printf("-- so thuc, giong tung bit ban tuan tu multiply_blocked\n");
    const int cases[][2] = {{97, 32}, {200, 48}, {257, 64}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 900 + n, A, B);
        for (mm::Kernel k : {mm::Kernel::Simd, mm::Kernel::NoVec}) {
            Matrix S(A.rows(), A.rows());
            mm::multiply_blocked(A.data(), B.data(), S.data(), n, b, k);
            std::string failed;
            for (int t = 1; t <= kMaxThreads; ++t) {
                set_threads(t);
                for (mm::Schedule s : kSchedules) {
                    if (!identical(rows(A, B, b, s, k), S)) failed += where(t, s);
                }
            }
            check(failed.empty(), std::string("[") + mm::kernel_name(k) + "] n = " + std::to_string(n) + ", b = " +
                                      std::to_string(b) + ": 1-12 thread, ca hai lich giong tung bit multiply_blocked" +
                                      (failed.empty() ? "" : ", sai o (thread/lich):" + failed));
        }
        set_threads(kMaxThreads);
        const mm::CheckResult r = mm::compare_with_reference(A, B, rows(A, B, b, mm::Schedule::Static));
        check(r.ok, "n = " + std::to_string(n) + ", 12 thread, static: dat nguong so voi tham chieu: " + mm::describe(r));
    }
}

}  // namespace

int main() {
    try {
        test_api();
        test_exact();
        test_uniform();
    } catch (const std::exception& e) {
        check(false, std::string("ngoai le khong mong doi: ") + e.what());
    }

    if (g_failures == 0) {
        std::printf("test_par_1d: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_par_1d: %d kiem tra that bai\n", g_failures);
    return 1;
}
