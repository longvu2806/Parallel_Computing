// Nhân ma trận tuần tự chuẩn (không chia block), 6 thứ tự vòng lặp.
//
//   build/mm_seq.exe --n 1024 [--variant ikj] [--seed 42] [--run 0]
//                    [--input file] [--output file] [--csv file] [--verify]
//
// --variant: ijk, ikj (mặc định, thứ tự nhanh nhất với ma trận lưu theo hàng), jik, jki, kij, kji.
// Bản tuần tự chỉ chạy 1 thread; truyền --threads khác 1 là lỗi.
// --verify: n <= 512 so từng phần tử với bản tham chiếu, n lớn hơn dùng Freivalds (common/verify).

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/matrix.hpp"
#include "common/timer.hpp"
#include "common/verify.hpp"
#include "matmul/mm_seq.hpp"

int main(int argc, char** argv) {
    pc::PhaseTimer timer;
    const Options opt = parse_options_or_exit(argc, argv);
    try {
        const std::string variant = opt.variant.empty() ? "ikj" : opt.variant;
        const mm::LoopOrder order = mm::parse_loop_order(variant);
        if (opt.threads != 1) {
            throw std::invalid_argument("mm_seq la ban tuan tu, khong nhan --threads khac 1");
        }

        Matrix A, B;
        const long long n = load_matmul_input(opt, A, B, timer);
        const std::size_t un = static_cast<std::size_t>(n);

        Matrix C;
        {
            auto scope = timer.measure(pc::Phase::Init);
            C = Matrix(un, un);
        }

        {
            auto scope = timer.measure(pc::Phase::Compute);
            mm::multiply(order, A.data(), B.data(), C.data(), un);
        }

        // Kiểm tra nằm ngoài ba pha (chỉ tính vào t_total).
        if (opt.verify) {
            const mm::CheckResult r = mm::verify_product(A, B, C);
            if (!r.ok) {
                std::fprintf(stderr, "Loi --verify: %s\n", mm::describe(r).c_str());
                return 1;
            }
        }
        const double check = mm::checksum(C);

        if (!opt.output.empty()) {
            auto scope = timer.measure(pc::Phase::IO);
            write_matrix_result(opt.output, C);
        }

        CsvRecord rec = make_csv_record("matmul", opt, n, timer, check);
        rec.variant = variant;
        rec.block = 0;       // không chia block
        rec.threads = 1;
        rec.pinned = false;
        if (!opt.csv.empty()) append_csv_row(opt.csv, rec);

        std::printf("mm_seq variant=%s n=%lld check=%.17g t_init=%.6f t_compute=%.6f t_io=%.6f t_total=%.6f%s\n",
                    variant.c_str(), n, check, rec.t_init, rec.t_compute, rec.t_io, rec.t_total,
                    opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
