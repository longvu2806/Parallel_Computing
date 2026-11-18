// Tich vo huong hai vector, ban tuan tu.
//
//   build/dot_seq.exe --n 100000000 [--variant simd|plain] [--seed 42] [--run 0]
//                     [--input file] [--output file] [--csv file] [--verify]
//
// --variant: simd (mac dinh, ban tuan tu tot nhat, la moc T1 cua E1) hoac plain.
// Ban tuan tu chi chay 1 thread; truyen --threads khac 1 la loi.

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/matrix.hpp"
#include "common/timer.hpp"
#include "dot_product/dot_kernel.hpp"

int main(int argc, char** argv) {
    pc::PhaseTimer timer;
    const Options opt = parse_options_or_exit(argc, argv);
    try {
        const std::string variant = opt.variant.empty() ? "simd" : opt.variant;
        if (variant != "simd" && variant != "plain") {
            throw std::invalid_argument("--variant cua dot_seq phai la simd hoac plain: " + variant);
        }
        if (opt.threads != 1) {
            throw std::invalid_argument("dot_seq la ban tuan tu, khong nhan --threads khac 1");
        }

        Matrix x, y;
        const long long n = load_dot_input(opt, x, y, timer);

        double result = 0.0;
        {
            auto scope = timer.measure(pc::Phase::Compute);
            result = (variant == "simd") ? dot::seq_simd(x.data(), y.data(), n)
                                         : dot::seq_plain(x.data(), y.data(), n);
        }

        // Kiem tra nam ngoai ba pha (chi tinh vao t_total).
        if (opt.verify) {
            const dot::Reference ref = dot::reference(x.data(), y.data(), n);
            if (!dot::matches(ref, result)) {
                std::fprintf(stderr, "Loi --verify: ket qua %.17g, tham chieu %.17g, lech %.3g > %.3g\n",
                             result, ref.value, result - ref.value, ref.bound);
                return 1;
            }
        }

        if (!opt.output.empty()) {
            auto scope = timer.measure(pc::Phase::IO);
            write_dot_result(opt.output, result);
        }

        CsvRecord rec = make_csv_record("dot", opt, n, timer, result);
        rec.variant = variant;
        rec.block = 0;       // khong chia block
        rec.threads = 1;
        rec.pinned = false;
        if (!opt.csv.empty()) append_csv_row(opt.csv, rec);

        std::printf("dot_seq variant=%s n=%lld result=%.17g t_init=%.6f t_compute=%.6f t_io=%.6f t_total=%.6f%s\n",
                    variant.c_str(), n, result, rec.t_init, rec.t_compute, rec.t_io, rec.t_total,
                    opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
