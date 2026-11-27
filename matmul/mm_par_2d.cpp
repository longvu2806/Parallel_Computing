// Nhân ma trận song song OpenMP, phân hoạch 2D theo tile (việc D5).
//
//   build/mm_par_2d.exe --n 2000 [--b 64] --threads 6 [--schedule static|dynamic] [--pin 0|1]
//                       [--variant simd|novec] [--seed 42] [--run 0]
//                       [--input file] [--output file] [--csv file] [--verify]
//
// Mỗi tile b x b của C là một đơn vị việc; collapse(2) gộp lưới tile thành một danh sách, chia cho
// các thread theo --schedule (matmul/mm_par_2d.hpp, có giải thích vì sao chọn tile vuông).
// Bên trong tile gọi kernel block mm::block_multiply như mm_block_seq, nên kết quả giống từng bit
// mm_block_seq cùng b.
// --b: block size của kernel (mặc định 64; E3 trên máy đo cho thấy b = 48 tốt nhất).
// --schedule: static (mặc định) hoặc dynamic (chunk 1), cho thí nghiệm E6.
// --variant: simd (mặc định) hoặc novec (cấm vector hóa), cho thí nghiệm E4 ở nhiều thread.
//            Thêm "-w<số>" để đổi bề rộng tile, ví dụ simd-w4 là tile b x 4b
//            (chỉ dùng cho thí nghiệm phụ scripts/experiments/tile_shape.conf).
// --pin 1: ghim mỗi thread vào một lõi (common/affinity), cho thí nghiệm E7.
// --verify: n <= 512 so từng phần tử với bản tham chiếu, n lớn hơn dùng Freivalds (common/verify).

#include <omp.h>

#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>

#include "common/affinity.hpp"
#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/matrix.hpp"
#include "common/timer.hpp"
#include "common/verify.hpp"
#include "matmul/mm_par_2d.hpp"

namespace {

// Vùng song song rỗng; trả về số thread thực tế của đội.
int warm_up_team() {
    int team = 0;
#pragma omp parallel default(none) shared(team)
    {
#pragma omp single
        team = omp_get_num_threads();
    }
    return team;
}

// Đặt số thread (và ghim nếu --pin 1), rồi chạy một vùng song song rỗng để libgomp tạo sẵn
// đội thread. Nếu không, lần tạo thread đầu tiên rơi vào pha compute.
int setup_threads(const Options& opt) {
    if (opt.pin) {
        pc::pin_omp_threads(opt.threads);  // tự gọi omp_set_num_threads, tắt dynamic
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
        const mm::TileVariant tv = mm::parse_tile_variant(variant);
        const mm::Schedule schedule = mm::parse_schedule(opt.schedule);

        // Tạo đội thread tính vào pha init (cùng với cấp phát, sinh dữ liệu).
        int team = 0;
        {
            auto scope = timer.measure(pc::Phase::Init);
            team = setup_threads(opt);
        }
        if (team != opt.threads) {
            throw std::runtime_error("OpenMP chi tao duoc " + std::to_string(team) + " thread, yeu cau " +
                                     std::to_string(opt.threads));
        }

        Matrix A, B;
        const long long n = load_matmul_input(opt, A, B, timer);
        if (n > std::numeric_limits<int>::max()) throw std::invalid_argument("n qua lon cho kernel (int)");
        const std::size_t un = static_cast<std::size_t>(n);

        Matrix C;
        {
            auto scope = timer.measure(pc::Phase::Init);
            C = Matrix(un, un);
        }

        // Mỗi tile tự gán 0 phần C của mình, tính vào pha compute như mm_block_seq.
        {
            auto scope = timer.measure(pc::Phase::Compute);
            mm::multiply_tiles(A.data(), B.data(), C.data(), static_cast<int>(n), opt.b, schedule, tv.kernel,
                               tv.width_blocks);
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
        if (!opt.csv.empty()) append_csv_row(opt.csv, rec);

        std::printf("mm_par_2d variant=%s b=%d tile=%dx%d threads=%d schedule=%s pin=%d n=%lld check=%.17g "
                    "t_init=%.6f t_compute=%.6f t_io=%.6f t_total=%.6f%s\n",
                    variant.c_str(), opt.b, opt.b, opt.b * tv.width_blocks, opt.threads,
                    mm::schedule_name(schedule), opt.pin ? 1 : 0, n, check, rec.t_init, rec.t_compute, rec.t_io,
                    rec.t_total, opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
