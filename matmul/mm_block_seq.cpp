// Nhân ma trận tuần tự chia block/tile.
//
//   build/mm_block_seq.exe --n 1024 [--b 64] [--seed 42] [--run 0]
//                          [--input file] [--output file] [--csv file] [--verify]
//
// Ba vòng ngoài duyệt block ii, kk, jj; mỗi bộ ba block gọi mm::block_multiply (matmul/mm_kernel.hpp),
// bên trong block theo thứ tự ikj. Block ở biên nhỏ hơn khi n không chia hết cho b; b >= n là một block.
// --b: block size (mặc định 64), dùng cho thí nghiệm E3 (khảo sát block size).
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
#include "matmul/mm_kernel.hpp"

int main(int argc, char** argv) {
    pc::PhaseTimer timer;
    const Options opt = parse_options_or_exit(argc, argv);
    try {
        // Hiện chỉ có một biến thể kernel; bản tối ưu (D4) sẽ thêm biến thể cho thí nghiệm E4.
        const std::string variant = opt.variant.empty() ? "ikj" : opt.variant;
        if (variant != "ikj") {
            throw std::invalid_argument("--variant cua mm_block_seq hien chi co ikj: " + variant);
        }
        if (opt.threads != 1) {
            throw std::invalid_argument("mm_block_seq la ban tuan tu, khong nhan --threads khac 1");
        }

        Matrix A, B;
        const long long n = load_matmul_input(opt, A, B, timer);
        const std::size_t un = static_cast<std::size_t>(n);

        Matrix C;
        {
            auto scope = timer.measure(pc::Phase::Init);
            C = Matrix(un, un);
        }

        // Gán 0 cho C nằm trong multiply_blocked, tính vào pha compute như mm_seq.
        {
            auto scope = timer.measure(pc::Phase::Compute);
            mm::multiply_blocked(A.data(), B.data(), C.data(), static_cast<int>(n), opt.b);
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
        rec.block = opt.b;
        rec.threads = 1;
        rec.pinned = false;
        if (!opt.csv.empty()) append_csv_row(opt.csv, rec);

        std::printf("mm_block_seq b=%d n=%lld check=%.17g t_init=%.6f t_compute=%.6f t_io=%.6f t_total=%.6f%s\n",
                    opt.b, n, check, rec.t_init, rec.t_compute, rec.t_io, rec.t_total,
                    opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
