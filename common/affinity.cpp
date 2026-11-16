#include "common/affinity.hpp"

#include <omp.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // windows.h không được định nghĩa macro min/max
#endif
#include <windows.h>
#endif

namespace pc {

std::vector<int> pin_plan(const CpuTopology& topo, int threads) {
    const int available = topo.logical_count();
    if (threads < 1 || threads > available) {
        throw std::invalid_argument("--threads " + std::to_string(threads) +
                                    " khong ghim duoc: may co " + std::to_string(available) +
                                    " CPU logic");
    }

    // Lõi mạnh trước; cùng loại thì giữ thứ tự theo chỉ số CPU (stable_sort).
    std::vector<const CpuCore*> order;
    for (const CpuCore& c : topo.cores) order.push_back(&c);
    std::stable_sort(order.begin(), order.end(), [](const CpuCore* a, const CpuCore* b) {
        return a->efficiency > b->efficiency;
    });

    // Vòng r lấy CPU logic thứ r của mỗi lõi: r = 0 là một thread mỗi lõi vật lý,
    // r = 1 là luồng SMT thứ hai.
    std::vector<int> plan;
    for (std::size_t r = 0; static_cast<int>(plan.size()) < threads; ++r) {
        for (const CpuCore* c : order) {
            if (r < c->cpus.size()) plan.push_back(c->cpus[r]);
            if (static_cast<int>(plan.size()) == threads) break;
        }
    }
    return plan;
}

#ifdef _WIN32

CpuTopology query_topology() {
    if (GetActiveProcessorGroupCount() != 1) {
        throw std::runtime_error("ghim thread chi ho tro may co mot processor group (<= 64 CPU logic)");
    }

    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
        throw std::runtime_error("GetProcessAffinityMask loi " + std::to_string(GetLastError()));
    }

    // Lần gọi đầu chỉ để biết cần bao nhiêu byte.
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) {
        throw std::runtime_error("GetLogicalProcessorInformationEx loi " + std::to_string(GetLastError()));
    }
    std::vector<unsigned char> buf(len);
    auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &len)) {
        throw std::runtime_error("GetLogicalProcessorInformationEx loi " + std::to_string(GetLastError()));
    }

    // Kết quả là các bản ghi có độ dài khác nhau nằm liền nhau; mỗi bản ghi là một lõi vật lý.
    CpuTopology topo;
    for (DWORD off = 0; off < len;) {
        const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
        if (info->Relationship == RelationProcessorCore) {
            const PROCESSOR_RELATIONSHIP& core = info->Processor;
            CpuCore c;
            c.efficiency = core.EfficiencyClass;
            const KAFFINITY mask = core.GroupMask[0].Mask & process_mask;
            for (int cpu = 0; cpu < static_cast<int>(sizeof(KAFFINITY) * 8); ++cpu) {
                if ((mask >> cpu) & 1) c.cpus.push_back(cpu);
            }
            if (!c.cpus.empty()) topo.cores.push_back(c);  // bỏ lõi mà tiến trình không được dùng
        }
        off += info->Size;
    }
    if (topo.cores.empty()) throw std::runtime_error("khong doc duoc loi CPU nao");

    std::sort(topo.cores.begin(), topo.cores.end(), [](const CpuCore& a, const CpuCore& b) {
        return a.cpus.front() < b.cpus.front();
    });
    return topo;
}

std::vector<int> pin_omp_threads(int threads) {
    const std::vector<int> plan = pin_plan(query_topology(), threads);

    // Tắt dynamic để libgomp tạo đúng số thread đã yêu cầu; các vùng song song sau
    // dùng lại đúng các thread đã ghim ở đây.
    omp_set_dynamic(0);
    omp_set_num_threads(threads);

    std::vector<DWORD> error(threads, 0);  // mỗi thread chỉ ghi phần tử của mình
    int team = 0;
#pragma omp parallel
    {
        const int t = omp_get_thread_num();
#pragma omp single
        team = omp_get_num_threads();
        if (t < threads) {
            const DWORD_PTR mask = DWORD_PTR(1) << plan[t];
            if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) error[t] = GetLastError();
        }
    }

    if (team != threads) {
        throw std::runtime_error("OpenMP tao " + std::to_string(team) + " thread thay vi " +
                                 std::to_string(threads));
    }
    for (int t = 0; t < threads; ++t) {
        if (error[t] != 0) {
            throw std::runtime_error("SetThreadAffinityMask loi " + std::to_string(error[t]) +
                                     " khi ghim thread " + std::to_string(t) + " vao CPU " +
                                     std::to_string(plan[t]));
        }
    }
    return plan;
}

int current_cpu() { return static_cast<int>(GetCurrentProcessorNumber()); }

#else  // không phải Windows

CpuTopology query_topology() {
    throw std::runtime_error("ghim thread chi ho tro tren Windows");
}

std::vector<int> pin_omp_threads(int /*threads*/) {
    throw std::runtime_error("ghim thread chi ho tro tren Windows");
}

int current_cpu() { return -1; }

#endif

}  // namespace pc
