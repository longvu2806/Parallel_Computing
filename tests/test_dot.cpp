// Kiem thu tich vo huong (dot_product/dot_kernel.hpp): 4 ham seq_plain, seq_simd, omp_plain,
// omp_simd va ham tham chieu dung cho --verify.
// Tra ma thoat 0 neu moi kiem tra dat, 1 neu co loi.

#include <omp.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "common/matrix.hpp"
#include "dot_product/dot_kernel.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using Kernel = double (*)(const double*, const double*, long long);

struct NamedKernel {
    const char* name;
    Kernel fn;
    bool parallel;
};

const NamedKernel kKernels[] = {
    {"seq_plain", dot::seq_plain, false},
    {"seq_simd", dot::seq_simd, false},
    {"omp_plain", dot::omp_plain, true},
    {"omp_simd", dot::omp_simd, true},
};

const int kThreadCounts[] = {1, 2, 3, 4, 6, 8, 12};

void set_threads(int t) {
    omp_set_dynamic(0);
    omp_set_num_threads(t);
}

// ------------------------------------------------------------ vi du tinh tay

void test_hand_example() {
    std::printf("-- vi du tinh tay\n");
    const double x[] = {1, 2, 3, 4};
    const double y[] = {5, 6, 7, 8};
    for (const NamedKernel& k : kKernels) {
        for (int t : {1, 3, 12}) {
            if (!k.parallel && t != 1) continue;
            set_threads(t);
            const double s = k.fn(x, y, 4);
            check(s == 70.0, std::string(k.name) + ", " + std::to_string(t) +
                                 " thread: [1 2 3 4].[5 6 7 8] = 70, duoc " + std::to_string(s));
        }
    }
}

// ------------------------------------------------------------ n = 0 va vector toan so 1

void test_edge_sizes() {
    std::printf("-- n = 0 va x.x voi x toan so 1\n");
    for (const NamedKernel& k : kKernels) {
        set_threads(4);
        check(k.fn(nullptr, nullptr, 0) == 0.0, std::string(k.name) + ": n = 0 cho 0");
    }
    // x.x = n voi x toan so 1 (muc 9.4). n den 10^6 van chinh xac vi n < 2^53.
    for (long long n : {1LL, 2LL, 3LL, 5LL, 7LL, 8LL, 13LL, 1000LL, 1000003LL}) {
        Matrix x = make_vector(static_cast<std::size_t>(n));
        x.fill(1.0);
        bool ok = true;
        for (const NamedKernel& k : kKernels) {
            for (int t : kThreadCounts) {
                if (!k.parallel && t != 1) continue;
                set_threads(t);
                if (k.fn(x.data(), x.data(), n) != static_cast<double>(n)) ok = false;
            }
        }
        check(ok, "x.x = " + std::to_string(n) + " cho moi ham, moi so thread");
    }
}

// ------------------------------------------------------------ so nguyen nho: khop tung bit

void test_small_int_exact() {
    std::printf("-- so nguyen nho: moi ham, moi so thread khop tung bit voi seq_plain\n");
    const long long sizes[] = {1, 2, 3, 4, 5, 7, 11, 12, 13, 16, 17, 63, 64, 65, 1000, 4099, 100003, 1000003};
    for (long long n : sizes) {
        Matrix x, y;
        generate_dot_input(static_cast<std::size_t>(n), 7 + static_cast<std::uint64_t>(n), x, y, Fill::SmallInt);
        const double want = dot::seq_plain(x.data(), y.data(), n);
        std::string bad;
        for (const NamedKernel& k : kKernels) {
            for (int t : kThreadCounts) {
                if (!k.parallel && t != 1) continue;
                set_threads(t);
                const double got = k.fn(x.data(), y.data(), n);
                if (got != want) {
                    bad += std::string(" ") + k.name + "/" + std::to_string(t) + "t=" + std::to_string(got);
                }
            }
        }
        check(bad.empty(), "n = " + std::to_string(n) + ", ket qua " + std::to_string(want) +
                               (bad.empty() ? "" : ", lech:" + bad));
    }
}

// ------------------------------------------------------------ lap lai nhieu lan de lo race

void test_repeat_for_races() {
    std::printf("-- lap 30 lan voi 12 thread, n = 2.000.003, so nguyen nho\n");
    const long long n = 2000003;
    Matrix x, y;
    generate_dot_input(static_cast<std::size_t>(n), 2026, x, y, Fill::SmallInt);
    const double want = dot::seq_plain(x.data(), y.data(), n);
    set_threads(12);
    int mismatches = 0;
    for (int r = 0; r < 30; ++r) {
        if (dot::omp_plain(x.data(), y.data(), n) != want) ++mismatches;
        if (dot::omp_simd(x.data(), y.data(), n) != want) ++mismatches;
    }
    check(mismatches == 0, "60 lan chay song song khop tung bit, so lan lech: " + std::to_string(mismatches));
}

// ------------------------------------------------------------ so thuc: trong can sai so

void test_uniform_within_bound() {
    std::printf("-- so thuc trong [-1, 1): moi ham nam trong can sai so cua tham chieu\n");
    for (long long n : {1LL, 17LL, 1000LL, 100003LL, 4000037LL}) {
        Matrix x, y;
        generate_dot_input(static_cast<std::size_t>(n), 42, x, y);
        const dot::Reference ref = dot::reference(x.data(), y.data(), n);
        double worst = 0.0;  // max |lech| / can
        bool ok = ref.bound > 0.0;
        for (const NamedKernel& k : kKernels) {
            for (int t : kThreadCounts) {
                if (!k.parallel && t != 1) continue;
                set_threads(t);
                const double got = k.fn(x.data(), y.data(), n);
                if (!dot::matches(ref, got)) ok = false;
                worst = std::fmax(worst, std::fabs(got - ref.value) / ref.bound);
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "n = %lld: tham chieu %.6g, can %.2e, lech lon nhat = %.2e lan can", n,
                      ref.value, ref.bound, worst);
        check(ok, buf);
    }
}

// ------------------------------------------------------------ ham kiem tra phai bat duoc loi

void test_reference_detects_errors() {
    std::printf("-- ham kiem tra (--verify) bat duoc ket qua sai va chap nhan sai so hop le\n");
    const long long n = 100000;
    Matrix x, y;
    generate_dot_input(static_cast<std::size_t>(n), 5, x, y);
    const dot::Reference ref = dot::reference(x.data(), y.data(), n);
    check(dot::matches(ref, ref.value), "tham chieu dat khi so voi chinh no");
    check(!dot::matches(ref, ref.value + 10.0 * ref.bound), "lech 10 lan can bi bat");

    // Bo sot mot so hang (loi cai dat dien hinh, vi du sai bien vong lap): phai bi bat.
    const double missing_last = dot::seq_plain(x.data(), y.data(), n - 1);
    check(!dot::matches(ref, missing_last), "thieu so hang cuoi bi bat");

    check(!dot::matches(ref, std::nan("")), "NaN bi bat");

    // Triet tieu: (1e16 + 1) - 1e16. Cong theo thu tu, so 1 bi lam tron mat va ket qua la 0
    const double cx[] = {1e16, 1.0, -1e16};
    const double cy[] = {1.0, 1.0, 1.0};
    const dot::Reference cref = dot::reference(cx, cy, 3);
    const double plain = dot::seq_plain(cx, cy, 3);
    check(cref.value == 1.0 && plain == 0.0 && dot::matches(cref, plain),
          "triet tieu: tham chieu = 1, seq_plain = 0, van nam trong can sai so");
}

}  // namespace

int main() {
    std::printf("OpenMP: _OPENMP = %d, omp_get_num_procs() = %d\n", _OPENMP, omp_get_num_procs());
    test_hand_example();
    test_edge_sizes();
    test_small_int_exact();
    test_repeat_for_races();
    test_uniform_within_bound();
    test_reference_detects_errors();

    if (g_failures == 0) {
        std::printf("test_dot: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_dot: %d kiem tra that bai\n", g_failures);
    return 1;
}
