#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace pc {

using Clock = std::chrono::steady_clock;

// Thoi gian (giay) tu t0 den t1.
inline double seconds_between(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double>(t1 - t0).count();
}

enum class Phase : std::size_t { Init = 0, Compute = 1, IO = 2 };

inline const char* phase_name(Phase p) {
    switch (p) {
        case Phase::Init:    return "init";
        case Phase::Compute: return "compute";
        case Phase::IO:      return "io";
    }
    return "?";
}

class PhaseTimer {
public:
    PhaseTimer() : origin_(Clock::now()) {}

    // Bat dau do pha p. Loi neu dang co pha khac (hoac chinh p) dang chay.
    void start(Phase p) {
        if (running_) {
            throw std::logic_error(std::string("PhaseTimer: start(") + phase_name(p) +
                                   ") khi pha '" + phase_name(active_) + "' dang chay");
        }
        active_ = p;
        running_ = true;
        t_start_ = Clock::now();  // lay thoi diem sau cung de khong do chi phi ham
    }

    // Ket thuc pha p, cong don vao tong cua pha. Loi neu p khong dang chay.
    void stop(Phase p) {
        const Clock::time_point t_end = Clock::now();  // lay thoi diem truoc tien
        if (!running_ || active_ != p) {
            throw std::logic_error(std::string("PhaseTimer: stop(") + phase_name(p) +
                                   ") khi pha nay khong chay");
        }
        acc_[index(p)] += seconds_between(t_start_, t_end);
        running_ = false;
    }

    // Tong thoi gian da do cua pha p (giay). Khong tinh lan dang chay do.
    double seconds(Phase p) const { return acc_[index(p)]; }

    // Tong ba pha (giay).
    double phase_sum() const { return acc_[0] + acc_[1] + acc_[2]; }

    // Wall clock tu luc tao timer (giay). Dung cho cot t_total.
    double total() const { return seconds_between(origin_, Clock::now()); }

    bool running() const { return running_; }

    // RAII: do pha p trong mot khoi lenh, ke ca khi co ngoai le.
    class Scope {
    public:
        Scope(PhaseTimer& t, Phase p) : timer_(&t), phase_(p) { timer_->start(phase_); }
        ~Scope() {
            if (timer_ != nullptr && timer_->running_ && timer_->active_ == phase_) {
                timer_->stop(phase_);
            }
        }
        Scope(Scope&& other) noexcept : timer_(other.timer_), phase_(other.phase_) {
            other.timer_ = nullptr;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope& operator=(Scope&&) = delete;

    private:
        PhaseTimer* timer_;
        Phase phase_;
    };

    [[nodiscard]] Scope measure(Phase p) { return Scope(*this, p); }

private:
    static constexpr std::size_t index(Phase p) { return static_cast<std::size_t>(p); }

    Clock::time_point origin_;
    Clock::time_point t_start_{};
    std::array<double, 3> acc_{0.0, 0.0, 0.0};
    Phase active_ = Phase::Init;
    bool running_ = false;
};

}  // namespace pc
