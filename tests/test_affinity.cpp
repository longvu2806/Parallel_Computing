// Kiem thu common/affinity: ke hoach ghim tren topology gia lap, va ghim that tren may dang chay.
// Tra ma thoat 0 neu moi kiem tra dat, 1 neu co loi.

#include "common/affinity.hpp"

#include <omp.h>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::string join(const std::vector<int>& v) {
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + std::to_string(v[i]);
    return s;
}

pc::CpuCore core(std::vector<int> cpus, int efficiency = 0) {
    pc::CpuCore c;
    c.cpus = std::move(cpus);
    c.efficiency = efficiency;
    return c;
}

// Topology giong may do i7-9850H: 6 loi, moi loi 2 CPU logic danh so lien nhau.
pc::CpuTopology six_cores_smt() {
    pc::CpuTopology t;
    for (int c = 0; c < 6; ++c) t.cores.push_back(core({2 * c, 2 * c + 1}));
    return t;
}

bool throws_invalid(const pc::CpuTopology& topo, int threads) {
    try {
        pc::pin_plan(topo, threads);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

// Moi thread lam viec ban mot luc, xen ke nhuong CPU de bo lap lich co co hoi chuyen thread,
// va ghi lai CPU dang chay. Tra ve so lan mot thread chay ngoai CPU da ghim.
int count_migrations(const std::vector<int>& plan, int samples) {
    int bad = 0;
#pragma omp parallel reduction(+ : bad)
    {
        const int t = omp_get_thread_num();
        volatile double x = 1.0;
        for (int s = 0; s < samples; ++s) {
            for (int i = 0; i < 20000; ++i) x = x * 1.0000001 + 1e-9;
            if (s % 16 == 0) std::this_thread::yield();
            if (pc::current_cpu() != plan[t]) ++bad;
        }
    }
    return bad;
}

}  // namespace

int main() {
    // 1. Ke hoach tren topology gia lap 6 loi x 2 SMT: loi vat ly truoc, SMT sau.
    const pc::CpuTopology smt = six_cores_smt();
    check(smt.logical_count() == 12, "6 loi x 2 SMT = 12 CPU logic");
    check(join(pc::pin_plan(smt, 1)) == "0", "1 thread -> CPU 0");
    check(join(pc::pin_plan(smt, 4)) == "0 2 4 6", "4 thread -> moi loi vat ly mot thread");
    check(join(pc::pin_plan(smt, 6)) == "0 2 4 6 8 10", "6 thread -> du 6 loi vat ly");
    check(join(pc::pin_plan(smt, 9)) == "0 2 4 6 8 10 1 3 5", "9 thread -> 3 thread dung SMT");
    check(join(pc::pin_plan(smt, 12)) == "0 2 4 6 8 10 1 3 5 7 9 11", "12 thread -> dung ca SMT");

    // 2. So thread sai bao loi.
    check(throws_invalid(smt, 0), "0 thread bao loi");
    check(throws_invalid(smt, 13), "13 thread tren may 12 CPU logic bao loi");

    // 3. May khong co SMT: moi loi mot CPU logic.
    pc::CpuTopology flat;
    for (int c = 0; c < 4; ++c) flat.cores.push_back(core({c}));
    check(join(pc::pin_plan(flat, 3)) == "0 1 2", "khong SMT: 3 thread -> 0 1 2");

    // 4. CPU lai (P-core/E-core): loi manh duoc dung truoc du co chi so lon hon.
    pc::CpuTopology hybrid;
    hybrid.cores = {core({0}, 0), core({1}, 0), core({2, 3}, 1), core({4, 5}, 1)};
    check(join(pc::pin_plan(hybrid, 2)) == "2 4", "lai: 2 thread -> 2 P-core");
    check(join(pc::pin_plan(hybrid, 4)) == "2 4 0 1", "lai: het P-core moi sang E-core");
    check(join(pc::pin_plan(hybrid, 6)) == "2 4 0 1 3 5", "lai: SMT cua P-core dung cuoi");

    // 5. Topology that cua may dang chay.
    pc::CpuTopology real;
    try {
        real = pc::query_topology();
    } catch (const std::exception& e) {
        check(false, std::string("doc topology: ") + e.what());
        std::printf("test_affinity: %d kiem tra that bai\n", g_failures);
        return 1;
    }
    std::printf("May nay: %zu loi vat ly, %d CPU logic.", real.cores.size(), real.logical_count());
    // Moi loi in dang (CPU logic | EfficiencyClass); class lon hon la loi manh hon.
    for (const pc::CpuCore& c : real.cores) std::printf(" (%s|%d)", join(c.cpus).c_str(), c.efficiency);
    std::printf("\n");

    std::vector<int> seen(64, 0);
    bool in_range = true;
    for (const pc::CpuCore& c : real.cores) {
        for (int cpu : c.cpus) {
            if (cpu < 0 || cpu >= 64) in_range = false;
            else ++seen[cpu];
        }
    }
    bool unique = true;
    for (int n : seen) unique = unique && n <= 1;
    check(in_range && unique, "moi CPU logic thuoc dung mot loi vat ly");
    check(real.logical_count() == omp_get_num_procs(), "so CPU logic khop omp_get_num_procs()");

    // 6. Ghim that: moi thread phai o yen tren CPU da ghim, ke ca o vung song song sau.
    const int cores = static_cast<int>(real.cores.size());
    const int logical = real.logical_count();
    std::vector<int> counts = {1, 2};
    if (cores > 2) counts.push_back(cores);
    if (logical > cores) counts.push_back(logical);
    for (int threads : counts) {
        std::vector<int> plan;
        try {
            plan = pc::pin_omp_threads(threads);
        } catch (const std::exception& e) {
            check(false, "ghim " + std::to_string(threads) + " thread: " + e.what());
            continue;
        }
        const int first = count_migrations(plan, 200);
        const int second = count_migrations(plan, 200);  // vung song song thu hai dung lai thread cu
        check(first == 0 && second == 0,
              std::to_string(threads) + " thread ghim vao CPU " + join(plan) + ", lech " +
                  std::to_string(first + second) + "/" + std::to_string(2 * 200 * threads) + " mau");
    }

    if (g_failures == 0) {
        std::printf("test_affinity: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_affinity: %d kiem tra that bai\n", g_failures);
    return 1;
}
