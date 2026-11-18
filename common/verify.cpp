// Cài đặt các hàm trong verify.hpp.

#include "common/verify.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>
#include <vector>

namespace mm {

namespace {

constexpr double kUnitRoundoff = 0x1.0p-53;

// 2 * gamma_(n+1): ngưỡng sai số tương đối so với (|A||B|)_ij.
double error_factor(std::size_t n) {
    const double nu = static_cast<double>(n + 1) * kUnitRoundoff;
    if (nu >= 0.5) throw std::invalid_argument("verify: n qua lon de uoc luong sai so");
    return 2.0 * nu / (1.0 - nu);
}

std::size_t check_square(const Matrix& A, const Matrix& B, const Matrix& C) {
    const std::size_t n = A.rows();
    if (A.empty() || A.cols() != n || B.rows() != n || B.cols() != n || C.rows() != n || C.cols() != n) {
        throw std::invalid_argument("verify: A, B, C phai la ma tran vuong cung cap n > 0");
    }
    return n;
}

// Ghi nhận một vị trí; giữ lại vị trí có |got - want| / bound lớn nhất.
// diff = |got - want| do nơi gọi tính (Freivalds tính bằng long double trước khi làm tròn).
// Trước vòng lặp đặt r.worst_ratio = -1 để vị trí đầu tiên luôn được ghi.
void record(CheckResult& r, std::size_t i, std::size_t j, double got, double want, double diff, double bound) {
    double ratio;
    if (std::isnan(got) || std::isnan(want) || std::isnan(diff)) ratio = INFINITY;
    else if (bound > 0.0) ratio = diff / bound;
    else ratio = diff > 0.0 ? INFINITY : 0.0;   // bound = 0 (hàng/cột toàn 0): phải khớp tuyệt đối
    if (ratio > r.worst_ratio) {
        r.worst_ratio = ratio;
        r.i = i;
        r.j = j;
        r.got = got;
        r.want = want;
        r.bound = bound;
    }
    if (!(ratio <= 1.0)) r.ok = false;
}

// Tích tham chiếu và (|A||B|) theo hàng, cộng dồn long double, thứ tự i-k-j để đọc B liên tiếp.
void reference_rows(const Matrix& A, const Matrix& B, Matrix* product, Matrix* abs_product) {
    const std::size_t n = A.rows();
    std::vector<long double> acc(n), acc_abs(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::fill(acc.begin(), acc.end(), 0.0L);
        std::fill(acc_abs.begin(), acc_abs.end(), 0.0L);
        for (std::size_t k = 0; k < n; ++k) {
            const long double a = A(i, k);
            const long double abs_a = std::fabs(a);
            const double* b = B.data() + k * n;
            for (std::size_t j = 0; j < n; ++j) {
                acc[j] += a * b[j];
                acc_abs[j] += abs_a * std::fabs(b[j]);
            }
        }
        for (std::size_t j = 0; j < n; ++j) {
            if (product) (*product)(i, j) = static_cast<double>(acc[j]);
            if (abs_product) (*abs_product)(i, j) = static_cast<double>(acc_abs[j]);
        }
    }
}

}  // namespace

Matrix reference_multiply(const Matrix& A, const Matrix& B) {
    const std::size_t n = A.rows();
    if (A.empty() || A.cols() != n || B.rows() != n || B.cols() != n) {
        throw std::invalid_argument("reference_multiply: A, B phai la ma tran vuong cung cap");
    }
    Matrix C(n, n);
    reference_rows(A, B, &C, nullptr);
    return C;
}

CheckResult compare_with_reference(const Matrix& A, const Matrix& B, const Matrix& C) {
    const std::size_t n = check_square(A, B, C);
    Matrix ref(n, n), abs_ref(n, n);
    reference_rows(A, B, &ref, &abs_ref);
    const double factor = error_factor(n);

    CheckResult r;
    r.method = "reference";
    r.worst_ratio = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const double got = C(i, j), want = ref(i, j);
            record(r, i, j, got, want, std::fabs(got - want), factor * abs_ref(i, j));
        }
    }
    return r;
}

CheckResult freivalds_check(const Matrix& A, const Matrix& B, const Matrix& C, std::uint64_t seed, int trials) {
    const std::size_t n = check_square(A, B, C);
    if (trials < 1) throw std::invalid_argument("freivalds_check: trials phai >= 1");
    const double factor = error_factor(n);

    std::mt19937_64 rng(seed);
    std::vector<long double> r(n), abs_r(n), Br(n), abs_Br(n);
    CheckResult res;
    res.method = "freivalds";
    res.worst_ratio = -1.0;
    for (int t = 0; t < trials; ++t) {
        // r_j = ±(1 + u), u trong [0, 1)
        for (std::size_t j = 0; j < n; ++j) {
            const std::uint64_t bits = rng();
            const long double magnitude = 1.0L + static_cast<long double>(bits >> 11) * 0x1.0p-53L;
            r[j] = (bits & 1u) ? -magnitude : magnitude;
            abs_r[j] = magnitude;
        }
        // Br và |B||r|
        for (std::size_t k = 0; k < n; ++k) {
            const double* b = B.data() + k * n;
            long double s = 0.0L, s_abs = 0.0L;
            for (std::size_t j = 0; j < n; ++j) {
                s += b[j] * r[j];
                s_abs += std::fabs(b[j]) * abs_r[j];
            }
            Br[k] = s;
            abs_Br[k] = s_abs;
        }
        // So (A(Br))_i với (Cr)_i; ngưỡng: factor * (|A||B||r|)_i
        for (std::size_t i = 0; i < n; ++i) {
            const double* a = A.data() + i * n;
            const double* c = C.data() + i * n;
            long double want = 0.0L, bound = 0.0L, got = 0.0L;
            for (std::size_t k = 0; k < n; ++k) {
                want += a[k] * Br[k];
                bound += std::fabs(a[k]) * abs_Br[k];
                got += c[k] * r[k];
            }
            record(res, i, 0, static_cast<double>(got), static_cast<double>(want),
                   static_cast<double>(std::fabs(got - want)), factor * static_cast<double>(bound));
        }
    }
    return res;
}

CheckResult verify_product(const Matrix& A, const Matrix& B, const Matrix& C) {
    return A.rows() <= kReferenceLimit ? compare_with_reference(A, B, C) : freivalds_check(A, B, C);
}

std::string describe(const CheckResult& r) {
    char buf[320];
    if (r.method == "freivalds") {
        std::snprintf(buf, sizeof buf,
                      "%s (freivalds): hang %zu, (Cr)_i = %.17g, (A(Br))_i = %.17g, lech %.3g, nguong %.3g",
                      r.ok ? "dung" : "SAI", r.i, r.got, r.want, std::fabs(r.got - r.want), r.bound);
    } else {
        std::snprintf(buf, sizeof buf,
                      "%s (so voi ban tham chieu): C(%zu,%zu) = %.17g, dung = %.17g, lech %.3g, nguong %.3g",
                      r.ok ? "dung" : "SAI", r.i, r.j, r.got, r.want, std::fabs(r.got - r.want), r.bound);
    }
    return buf;
}

double checksum(const Matrix& C) {
    double s = 0.0;
    const double* p = C.data();
    for (std::size_t k = 0; k < C.size(); ++k) s += p[k];
    return s;
}

}  // namespace mm
