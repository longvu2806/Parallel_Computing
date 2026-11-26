// Nhân ma trận song song theo thuật toán Cannon trên lưới q x q thread (việc D6).
//
//   build/mm_cannon.exe --n 2000 [--b 64] --threads 4|9|... [--variant barrier|nobarrier] [--pin 0|1]
//                       [--seed 42] [--run 0] [--input file] [--output file] [--csv file] [--verify]
//
// Thread t giữ khối C ở ô (t / q, t mod q) của lưới q x q; ở bước s cộng A[I][m] * B[m][J] với
// m = (I + J + s) mod q (matmul/mm_cannon.hpp). Trong mỗi bước gọi kernel block mm::block_multiply
// như mm_block_seq. Thứ tự cộng theo k bắt đầu từ khối m, nên với q >= 2 kết quả có thể lệch
// mm_block_seq ở vài bit cuối (cột check của CSV khác một chút); với 1 thread thì giống từng bit.
// --threads: phải là số chính phương (1, 4, 9, ...). Máy đo có 6 lõi vật lý nên 9 thread có dùng SMT.
// --b: block size của kernel bên trong mỗi khối lớn (mặc định 64; E3 trên máy đo: b = 48 tốt nhất).
// --variant: barrier (mặc định, có barrier giữa các bước như Cannon thật) hoặc nobarrier, cho E7.
// --schedule: không áp dụng (mỗi thread giữ cố định một ô của lưới); chỉ nhận static.
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
#include "matmul/mm_cannon.hpp"

namespace {

// Hai hàm dưới đây giống hệt matmul/mm_par_2d.cpp và matmul/mm_par_1d.cpp. Nếu sửa thì sửa cả ba nơi.

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
        const std::string variant = opt.variant.empty() ? "barrier" : opt.variant;
        const mm::CannonSync sync = mm::parse_cannon_sync(variant);
        const int q = mm::cannon_grid_side(opt.threads);
        if (opt.schedule != "static") {
            throw std::invalid_argument("mm_cannon khong co lich dynamic: moi thread giu co dinh mot o cua luoi");
        }

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

        // Mỗi thread tự gán 0 khối C của mình, tính vào pha compute như mm_block_seq.
        {
            auto scope = timer.measure(pc::Phase::Compute);
            mm::multiply_cannon(A.data(), B.data(), C.data(), static_cast<int>(n), opt.b, q, sync);
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

        std::printf("mm_cannon variant=%s b=%d grid=%dx%d threads=%d pin=%d n=%lld check=%.17g "
                    "t_init=%.6f t_compute=%.6f t_io=%.6f t_total=%.6f%s\n",
                    variant.c_str(), opt.b, q, q, opt.threads, opt.pin ? 1 : 0, n, check, rec.t_init, rec.t_compute,
                    rec.t_io, rec.t_total, opt.verify ? " verify=OK" : "");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Loi: %s\n", e.what());
        return 2;
    }
}
