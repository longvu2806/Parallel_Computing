#pragma once

// Ghi kết quả đo ra CSV theo hợp đồng trong README (mục "Dòng CSV kết quả").
// Mỗi lần chạy chương trình ghi thêm ĐÚNG MỘT dòng; header chỉ ghi khi file chưa có hoặc rỗng.
//
// Cách dùng ở cuối main(), sau khi đã đo xong mọi pha:
//     if (!opt.csv.empty()) append_csv_row(opt.csv, make_csv_record("dot", opt, n, timer, result));
// Việc ghi CSV không tính vào t_io: t_total được chốt lúc gọi make_csv_record.

#include <string>

#include "common/cli.hpp"
#include "common/timer.hpp"

// Một dòng CSV; thứ tự trường đúng thứ tự cột.
struct CsvRecord {
    std::string problem;            // "dot" hoặc "matmul"
    std::string variant;            // biến thể thực sự đã chạy (ijk, ikj, barrier, ...)
    long long n = 0;
    int block = 0;                  // block size; bản không chia block có thể để nguyên giá trị --b
    int threads = 1;
    std::string sched;              // static | dynamic
    bool pinned = false;
    int run = 0;
    unsigned long long seed = 0;
    double t_init = 0.0;            // giây
    double t_compute = 0.0;
    double t_io = 0.0;
    double t_total = 0.0;
    double check = 0.0;             // dot: chính kết quả; matmul: tổng các phần tử của C
};

// Dòng header, không kèm ký tự xuống dòng.
const char* csv_header();

// Định dạng một dòng (không kèm xuống dòng). Thời gian ghi %.9g, check ghi %.17g.
// Ném std::invalid_argument nếu trường chữ chứa dấu phẩy, dấu nháy kép hoặc xuống dòng.
std::string format_csv_row(const CsvRecord& r);

// Ghi thêm một dòng vào file path; tạo file và ghi header nếu file chưa có hoặc rỗng.
// Ném std::runtime_error nếu không mở/ghi được file, hoặc file đã có header khác
// (tránh trộn kết quả vào nhầm file).
void append_csv_row(const std::string& path, const CsvRecord& r);

// Điền bản ghi từ tham số dòng lệnh và timer: variant, block, threads, sched, pinned, run, seed
// lấy từ opt; t_init, t_compute, t_io từ timer; t_total = timer.total() tại thời điểm gọi.
// Nếu opt.variant rỗng (người dùng không truyền --variant), chương trình nên tự gán
// r.variant = tên biến thể mặc định trước khi ghi.
CsvRecord make_csv_record(const std::string& problem, const Options& opt, long long n,
                          const pc::PhaseTimer& timer, double check);
