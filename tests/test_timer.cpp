// Kiem thu common/timer.hpp. Tra ma thoat 0 neu moi kiem tra dat, 1 neu co loi.

#include "common/timer.hpp"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++g_failures;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Buoc nho nhat quan sat duoc cua steady_clock (giay).
double observed_resolution() {
    double best = 1.0;
    for (int r = 0; r < 1000; ++r) {
        const auto t0 = pc::Clock::now();
        auto t1 = pc::Clock::now();
        while (t1 == t0) t1 = pc::Clock::now();
        const double d = pc::seconds_between(t0, t1);
        if (d < best) best = d;
    }
    return best;
}

}  // namespace

int main() {
    // 1. Do phan giai: tai lieu ly thuyet do duoc 100 ns tren MSYS2; yeu cau < 1 us.
    const double res = observed_resolution();
    std::printf("steady_clock: buoc nho nhat quan sat duoc = %.1f ns, is_steady = %d\n",
                res * 1e9, static_cast<int>(pc::Clock::is_steady));
    check(pc::Clock::is_steady, "steady_clock la dong ho don dieu");
    check(res < 1e-6, "do phan giai nho hon 1 us");

    // 2. Do mot pha: ngu 20 ms phai cho >= 19 ms (sleep chi co the dai hon, khong ngan hon).
    pc::PhaseTimer timer;
    timer.start(pc::Phase::Compute);
    sleep_ms(20);
    timer.stop(pc::Phase::Compute);
    const double c1 = timer.seconds(pc::Phase::Compute);
    std::printf("compute sau 1 lan ngu 20 ms: %.6f s\n", c1);
    check(c1 >= 0.019 && c1 < 0.5, "pha compute do dung khoang 20 ms");
    check(timer.seconds(pc::Phase::Init) == 0.0 && timer.seconds(pc::Phase::IO) == 0.0,
          "pha khong do bang 0");

    // 3. Cong don: do them 20 ms vao compute.
    timer.start(pc::Phase::Compute);
    sleep_ms(20);
    timer.stop(pc::Phase::Compute);
    const double c2 = timer.seconds(pc::Phase::Compute);
    check(c2 >= c1 + 0.019, "pha compute cong don qua nhieu lan do");

    // 4. RAII Scope, ke ca khi co ngoai le.
    {
        auto scope = timer.measure(pc::Phase::Init);
        sleep_ms(10);
    }
    check(timer.seconds(pc::Phase::Init) >= 0.009, "Scope do pha init");
    check(!timer.running(), "Scope tat pha khi ra khoi khoi lenh");
    try {
        auto scope = timer.measure(pc::Phase::IO);
        sleep_ms(5);
        throw std::runtime_error("gia lap loi I/O");
    } catch (const std::runtime_error&) {
    }
    check(timer.seconds(pc::Phase::IO) >= 0.004 && !timer.running(),
          "Scope tat pha khi co ngoai le");

    // 5. Dung sai: hai pha chong nhau, stop sai pha, stop khi khong chay.
    bool threw = false;
    timer.start(pc::Phase::Init);
    try { timer.start(pc::Phase::Compute); } catch (const std::logic_error&) { threw = true; }
    check(threw, "start khi pha khac dang chay thi bao loi");
    threw = false;
    try { timer.stop(pc::Phase::IO); } catch (const std::logic_error&) { threw = true; }
    check(threw, "stop sai pha thi bao loi");
    timer.stop(pc::Phase::Init);
    threw = false;
    try { timer.stop(pc::Phase::Init); } catch (const std::logic_error&) { threw = true; }
    check(threw, "stop khi khong co pha nao chay thi bao loi");

    // 6. t_total la wall clock, khong nho hon tong ba pha.
    const double sum = timer.phase_sum();
    const double total = timer.total();
    std::printf("phase_sum = %.6f s, total = %.6f s\n", sum, total);
    check(total >= sum, "total >= tong ba pha");

    if (g_failures == 0) {
        std::printf("test_timer: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_timer: %d kiem tra that bai\n", g_failures);
    return 1;
}
