// Tich vo huong hai vector, ban song song OpenMP.
//
//   build/dot_omp.exe --n 100000000 --threads 6 [--variant simd|plain] [--pin 0|1]
//                     [--seed 42] [--run 0] [--input file] [--output file] [--csv file] [--verify]
//
// --variant: simd (mac dinh, "parallel for simd") hoac plain ("parallel for", khong vectorize).
// Lap lich luon la schedule(static): moi phan tu ton cong nhu nhau, chia deu la toi uu.

#include <omp.h>

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "common/affinity.hpp"
#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/matrix.hpp"
#include "common/timer.hpp"
#include "dot_product/dot_kernel.hpp"

namespace {

// Vung song song rong; tra ve so thread thuc te cua doi.
int warm_up_team() {
    int team = 0;
#pragma omp parallel default(none) shared(team)
    {
#pragma omp single
        team = omp_get_num_threads();
    }
    return team;
}

// Dat so thread (va ghim neu --pin 1), roi chay mot vung song song rong de libgomp tao san
// doi thread. Neu khong, lan tao thread dau tien roi vao pha compute.
int setup_threads(const Options& opt) {
    if (opt.pin) {
        pc::pin_omp_threads(opt.threads);  // tu goi omp_set_num_threads, tat dynamic
    } else {
        omp_set_dynamic(0);
        omp_set_num_threads(opt.threads);
    }
    return warm_up_team();
}

}  // namespace

int main(int argc, char** argv) {
    pc::PhaseTimer timer;
    const Options opt = parse_options_or_exit(argc, argv);
    try {
        const std::string variant = opt.variant.empty() ? "simd" : opt.variant;
        if (variant != "simd" && variant != "plain") {
            throw std::invalid_argument("--variant cua dot_omp phai la simd hoac plain: " + variant);
        }
        if (opt.schedule != "static") {
            throw std::invalid_argument("dot_omp chi dung schedule static");
        }

        // Tao doi thread tinh vao pha init (cung voi cap phat, sinh du lieu).
        int team = 0;
        {
            auto scope = timer.measure(pc::Phase::Init);
            team = setup_threads(opt);
        }
        if (team != opt.threads) {
            throw std::runtime_error("OpenMP chi tao duoc " + std::to_string(team) + " thread, yeu cau " +
                                     std::to_string(opt.threads));
        }

        Matrix x, y;
        const long long n = load_dot_input(opt, x, y, timer);

        double result = 0.0;
        {
            auto scope = timer.measure(pc::Phase::Compute);
            result = (variant == "simd") ? dot::omp_simd(x.data(), y.data(), n)
                                         : dot::omp_plain(x.data(), y.data(), n);
        }

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
        rec.block = 0;  // khong chia block
        if (!opt.csv.empty()) append_csv_row(opt.csv, rec);

        std::printf("dot_omp variant=%s threads=%d pin=%d n=%lld result=%.17g t_init=%.6f t_compute=%.6f "
                    "t_io=%.6f t_total=%.6f%s\n",
                    variant.c_str(), opt.threads, opt.pin ? 1 : 0, n, result, rec.t_init, rec.t_compute,
                    rec.t_io, rec.t_total, opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
