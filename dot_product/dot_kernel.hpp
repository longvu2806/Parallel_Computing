#pragma once

// Cac ham tinh tich vo huong, dung chung cho dot_seq, dot_omp va tests/test_dot.
//
// Hai bien the cho moi ban (thi nghiem E1: "co va khong simd"):
//   plain - cong dong theo dung thu tu i = 0, 1, ..., n-1 (trong moi thread). Theo IEEE,
//           trinh bien dich khong duoc doi thu tu cong, nen vong nay khong vectorize that su
//           (tai lieu ly thuyet, muc 7.3).
//   simd  - #pragma omp simd reduction(+:s): cho phep doi thu tu cong de dung lenh vector
//           (AVX2: 4 double moi lenh). Ket qua co the lech plain o vai chu so cuoi; voi du lieu
//           so nguyen nho (Fill::SmallInt) thi phai khop tung bit.
//
// Ban song song (PCAM, xem bao cao chuong 3):
//   Partitioning  - moi tich x[i]*y[i] la mot tac vu.
//   Communication - cac tac vu doc lap, chi can gom tong.
//   Agglomeration - chia [0, n) thanh p doan lien tiep (schedule(static)), moi thread mot tong rieng;
//                   OpenMP reduction cong p tong rieng o cuoi, khong co tranh chap ghi.
//   Mapping       - moi doan mot thread, tinh, vi moi phan tu ton cong nhu nhau.

#include <cmath>
#include <cstddef>
#include <limits>

namespace dot {

// ---- Ban tuan tu ----

inline double seq_plain(const double* x, const double* y, long long n) {
    double s = 0.0;
    for (long long i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

inline double seq_simd(const double* x, const double* y, long long n) {
    double s = 0.0;
#pragma omp simd reduction(+ : s)
    for (long long i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

// ---- Ban song song ----
// So thread lay tu omp_set_num_threads (hoac pc::pin_omp_threads) da goi truoc do.
// x, y chi doc; s la bien reduction: moi thread co ban sao rieng khoi tao bang 0,
// cong vao bien goc mot lan khi ket thuc vung song song. Khong co bien dung chung nao bi ghi.

inline double omp_plain(const double* x, const double* y, long long n) {
    double s = 0.0;
#pragma omp parallel for schedule(static) default(none) shared(x, y, n) reduction(+ : s)
    for (long long i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

inline double omp_simd(const double* x, const double* y, long long n) {
    double s = 0.0;
#pragma omp parallel for simd schedule(static) default(none) shared(x, y, n) reduction(+ : s)
    for (long long i = 0; i < n; ++i) s += x[i] * y[i];
    return s;
}

// ---- Kiem tra ket qua (--verify, test) ----

struct Reference {
    double value;  // x.y tinh bang long double (80 bit tren GCC x86-64), coi nhu gia tri dung
    double bound;  // sai so cho phep cua mot ban tinh bang double
};

// Sai so lam tron cua tich vo huong tinh bang double, theo moi thu tu cong:
//   |s_hat - s| <= gamma_n * sum |x_i * y_i|,  gamma_n = n*u / (1 - n*u),  u = 2^-53
// (tai lieu ly thuyet, muc 2.4). Lay gap doi de bu sai so cua chinh ban tham chieu.
inline Reference reference(const double* x, const double* y, long long n) {
    long double s = 0.0L;
    long double abs_sum = 0.0L;
    for (long long i = 0; i < n; ++i) {
        const long double p = static_cast<long double>(x[i]) * static_cast<long double>(y[i]);
        s += p;
        abs_sum += std::fabs(p);
    }
    const double u = std::numeric_limits<double>::epsilon() / 2.0;
    const double nu = static_cast<double>(n) * u;
    const double gamma = nu / (1.0 - nu);
    return {static_cast<double>(s), 2.0 * gamma * static_cast<double>(abs_sum)};
}

inline bool matches(const Reference& ref, double value) {
    return std::fabs(value - ref.value) <= ref.bound;
}

}  // namespace dot
