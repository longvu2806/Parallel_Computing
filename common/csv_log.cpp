// Cài đặt các hàm trong csv_log.hpp.

#include "common/csv_log.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

const char* csv_header() {
    return "problem,variant,n,block,threads,sched,pinned,run,seed,t_init,t_compute,t_io,t_total,check";
}

namespace {

// Trường chữ không được làm vỡ cấu trúc CSV. Không dùng quoting để file luôn đơn giản.
void check_text_field(const char* name, const std::string& value) {
    if (value.find_first_of(",\"\r\n") != std::string::npos) {
        throw std::invalid_argument(std::string("CSV: truong ") + name +
                                    " khong duoc chua dau phay, nhay kep hoac xuong dong: " + value);
    }
}

}  // namespace

std::string format_csv_row(const CsvRecord& r) {
    check_text_field("problem", r.problem);
    check_text_field("variant", r.variant);
    check_text_field("sched", r.sched);

    char numbers[512];
    std::snprintf(numbers, sizeof numbers, "%lld,%d,%d,%s,%d,%d,%llu,%.9g,%.9g,%.9g,%.9g,%.17g",
                  r.n, r.block, r.threads, r.sched.c_str(), r.pinned ? 1 : 0, r.run, r.seed,
                  r.t_init, r.t_compute, r.t_io, r.t_total, r.check);
    return r.problem + "," + r.variant + "," + numbers;
}

void append_csv_row(const std::string& path, const CsvRecord& r) {
    const std::string row = format_csv_row(r) + "\n";   // kiểm tra trường trước khi đụng tới file

    // Nếu file đã có nội dung thì dòng đầu phải đúng header của hợp đồng.
    if (std::FILE* existing = std::fopen(path.c_str(), "rb")) {
        char first[1024] = {0};
        const bool has_line = std::fgets(first, sizeof first, existing) != nullptr;
        std::fclose(existing);
        if (has_line) {
            first[std::strcspn(first, "\r\n")] = '\0';
            if (std::strcmp(first, csv_header()) != 0) {
                throw std::runtime_error("CSV: file " + path + " da co header khac, khong ghi them");
            }
        }
    }

    std::FILE* f = std::fopen(path.c_str(), "ab");
    if (f == nullptr) throw std::runtime_error("CSV: khong mo duoc file " + path);
    std::fseek(f, 0, SEEK_END);
    const bool is_new = std::ftell(f) == 0;
    bool ok = true;
    if (is_new) ok = std::fprintf(f, "%s\n", csv_header()) > 0;
    ok = ok && std::fputs(row.c_str(), f) >= 0;
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) throw std::runtime_error("CSV: loi khi ghi file " + path);
}

CsvRecord make_csv_record(const std::string& problem, const Options& opt, long long n,
                          const pc::PhaseTimer& timer, double check) {
    CsvRecord r;
    r.problem = problem;
    r.variant = opt.variant;
    r.n = n;
    r.block = opt.b;
    r.threads = opt.threads;
    r.sched = opt.schedule;
    r.pinned = opt.pin;
    r.run = opt.run;
    r.seed = opt.seed;
    r.t_init = timer.seconds(pc::Phase::Init);
    r.t_compute = timer.seconds(pc::Phase::Compute);
    r.t_io = timer.seconds(pc::Phase::IO);
    r.t_total = timer.total();
    r.check = check;
    return r;
}
