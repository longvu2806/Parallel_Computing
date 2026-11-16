#pragma once

// Ghim thread OpenMP vào lõi vật lý bằng Win32 API.
//
// Vì sao không dùng OMP_PROC_BIND/OMP_PLACES: libgomp trên MSYS2 báo
// "Affinity not supported on this configuration" và bỏ qua hai biến này
// (tài liệu lý thuyết, mục 8.8).
//
// Vì sao không ghim cứng "thread t -> CPU 2t": cách đánh số CPU logic khác nhau
// giữa các máy. Ở đây đọc topology thật bằng GetLogicalProcessorInformationEx,
// biết CPU logic nào thuộc lõi vật lý nào, rồi mới lập kế hoạch ghim.
//
// Cách dùng trong main(), ngay sau khi đọc tham số và trước mọi vùng song song:
//     if (opt.pin) pc::pin_omp_threads(opt.threads);
//     else         omp_set_num_threads(opt.threads);
// libgomp giữ lại các thread cho các vùng song song sau (cùng số thread),
// nên chỉ cần ghim một lần.

#include <vector>

namespace pc {

// Một lõi vật lý: các CPU logic (chỉ số Windows) thuộc lõi này, tăng dần.
// Có SMT thì mỗi lõi có 2 CPU logic, không có SMT thì 1.
struct CpuCore {
    std::vector<int> cpus;
    int efficiency = 0;  // EfficiencyClass của Windows; lớn hơn là lõi mạnh hơn (P-core)
};

struct CpuTopology {
    std::vector<CpuCore> cores;  // sắp theo CPU logic nhỏ nhất của mỗi lõi

    int logical_count() const {
        int total = 0;
        for (const CpuCore& c : cores) total += static_cast<int>(c.cpus.size());
        return total;
    }
};

// Đọc topology của máy đang chạy. Chỉ giữ các CPU mà tiến trình được phép chạy.
// Ném std::runtime_error nếu không đọc được, không phải Windows,
// hoặc máy có hơn 64 CPU logic (nhiều processor group).
CpuTopology query_topology();

// Kế hoạch ghim: phần tử t là CPU logic dành cho thread t.
// Thứ tự: mỗi lõi vật lý một thread trước (lõi mạnh trước, rồi theo chỉ số),
// lấy CPU logic đầu tiên của lõi; khi hết lõi mới dùng CPU logic thứ hai của mỗi lõi (SMT).
// Ví dụ 6 lõi, CPU (0,1) (2,3) ... (10,11):
//     6 thread  -> 0 2 4 6 8 10
//     12 thread -> 0 2 4 6 8 10 1 3 5 7 9 11
// Ném std::invalid_argument nếu threads < 1 hoặc nhiều hơn số CPU logic.
std::vector<int> pin_plan(const CpuTopology& topo, int threads);

// Đặt số thread OpenMP (omp_set_num_threads, tắt dynamic) và ghim mỗi thread
// vào CPU logic theo pin_plan. Trả về kế hoạch đã áp dụng.
// Ném std::runtime_error nếu không ghim được, std::invalid_argument nếu threads sai.
std::vector<int> pin_omp_threads(int threads);

// CPU logic mà thread gọi hàm đang chạy; -1 nếu không phải Windows.
int current_cpu();

}  // namespace pc
