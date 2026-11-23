// Kiểm thử các bản nhân ma trận song song chia block (việc D7):
//   - tile (matmul/mm_par_2d.hpp, D5): đủ 1-12 thread, schedule static và dynamic.
//   - Cannon (D6): thêm sau.
// So với hàm tham chiếu của Vũ (common/verify: reference_multiply, compare_with_reference)
// và với bản tuần tự mm::multiply_blocked (matmul/mm_kernel.hpp).
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include <omp.h>

#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>

#include "common/matrix.hpp"
#include "common/verify.hpp"
#include "matmul/mm_kernel.hpp"
#include "matmul/mm_par_2d.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

bool identical(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (!(a[k] == b[k])) return false;
    }
    return true;
}

constexpr int kMaxThreads = 12;  // số CPU logic của máy đo và máy Đức
constexpr mm::Schedule kSchedules[] = {mm::Schedule::Static, mm::Schedule::Dynamic};

void set_threads(int t) {
    omp_set_dynamic(0);
    omp_set_num_threads(t);
}

// C = A * B bằng multiply_tiles; C ban đầu chứa NaN để chắc chắn mọi ô đều được ghi đè.
Matrix tiles(const Matrix& A, const Matrix& B, int b, mm::Schedule s, mm::Kernel k = mm::Kernel::Simd,
             int width_blocks = mm::kTileWidthBlocks) {
    const int n = static_cast<int>(A.rows());
    Matrix C(A.rows(), A.rows());
    C.fill(NAN);
    mm::multiply_tiles(A.data(), B.data(), C.data(), n, b, s, k, width_blocks);
    return C;
}

std::string config(int n, int b, int t, mm::Schedule s) {
    return "n = " + std::to_string(n) + ", b = " + std::to_string(b) + ", " + std::to_string(t) + " thread, " +
           mm::schedule_name(s);
}

// Ghi lại cấu hình sai dưới dạng " thread/lich" hoặc " thread/lich/w".
std::string where(int t, mm::Schedule s, int w = 0) {
    return " " + std::to_string(t) + "/" + mm::schedule_name(s) + (w ? "/w" + std::to_string(w) : "");
}

// ---------------------------------------------------------------- tile: tên lịch, tham số sai

void test_tile_api() {
    std::printf("-- tile: ten lich, tham so sai\n");
    bool names = true;
    for (mm::Schedule s : kSchedules) names = names && mm::parse_schedule(mm::schedule_name(s)) == s;
    bool threw = false;
    try { mm::parse_schedule("guided"); } catch (const std::invalid_argument&) { threw = true; }
    check(names && threw, "ten lich: doi qua lai dung, ten sai bao loi");

    double c = 1.0;
    threw = false;
    try { mm::multiply_tiles(&c, &c, &c, 1, 0); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "b = 0 bao loi");

    threw = false;
    try { mm::multiply_tiles(&c, &c, &c, 1, 1, mm::Schedule::Static, mm::Kernel::Simd, 0); }
    catch (const std::invalid_argument&) { threw = true; }
    check(threw, "be rong tile 0 block bao loi");

    // Tên biến thể của mm_par_2d: kernel, có thể kèm bề rộng tile -w<số>.
    const mm::TileVariant d = mm::parse_tile_variant("simd");
    const mm::TileVariant w1 = mm::parse_tile_variant("simd-w1");
    const mm::TileVariant nv = mm::parse_tile_variant("novec-w12");
    check(d.kernel == mm::Kernel::Simd && d.width_blocks == mm::kTileWidthBlocks && w1.kernel == mm::Kernel::Simd &&
              w1.width_blocks == 1 && nv.kernel == mm::Kernel::NoVec && nv.width_blocks == 12,
          "parse_tile_variant: simd, simd-w1, novec-w12");
    int rejected = 0;
    for (const char* bad : {"simd-w0", "simd-w", "simd-w65", "simd-w01", "simd-wx", "ikj-w4", "simd-w4x", "simd-w+4"}) {
        try { mm::parse_tile_variant(bad); } catch (const std::invalid_argument&) { ++rejected; }
    }
    check(rejected == 8, "parse_tile_variant tu choi 8 ten sai (w0, w65, w01, thieu so, ...)");

    c = 5.0;
    mm::multiply_tiles(nullptr, nullptr, &c, 0, 8);
    check(c == 5.0, "n = 0: khong lam gi");
}

// ---------------------------------------------------------------- tile: SmallInt, khớp tuyệt đối

// Dữ liệu số nguyên nhỏ: mọi phép tính chính xác, nên mọi số thread và mọi lịch
// đều phải khớp tuyệt đối với bản tham chiếu của Vũ.
// Gồm n không chia hết cho b hay 4b, b > n, b = 1, số tile ít hơn số thread (n = 5, b = 8: 1 tile).
void test_tile_exact() {
    std::printf("-- tile: SmallInt, 1-%d thread, khop tuyet doi voi tham chieu\n", kMaxThreads);
    const int cases[][2] = {{1, 1}, {5, 8}, {7, 1}, {37, 8}, {64, 16}, {100, 48}, {130, 48}, {50, 64}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 300 + n, A, B, Fill::SmallInt);
        const Matrix R = mm::reference_multiply(A, B);
        std::string failed;
        for (int t = 1; t <= kMaxThreads; ++t) {
            set_threads(t);
            for (mm::Schedule s : kSchedules) {
                if (!identical(tiles(A, B, b, s), R)) failed += where(t, s);
            }
        }
        check(failed.empty(), "n = " + std::to_string(n) + ", b = " + std::to_string(b) +
                                  ": moi so thread, ca hai lich khop tham chieu" +
                                  (failed.empty() ? "" : ", sai o (thread/lich):" + failed));
    }
}

// ---------------------------------------------------------------- tile: số thực

// Dữ liệu thực: kết quả phải giống từng bit multiply_blocked cùng b (cùng thứ tự cộng theo k),
// với mọi số thread, mọi lịch, cả hai kernel, các bề rộng tile 1, 2, 4 (mặc định), 7 block;
// và đạt ngưỡng sai số so với tham chiếu.
void test_tile_uniform() {
    std::printf("-- tile: so thuc, giong tung bit ban tuan tu multiply_blocked\n");
    const int cases[][2] = {{97, 32}, {200, 48}, {257, 64}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 500 + n, A, B);
        for (mm::Kernel k : {mm::Kernel::Simd, mm::Kernel::NoVec}) {
            Matrix S(A.rows(), A.rows());
            mm::multiply_blocked(A.data(), B.data(), S.data(), n, b, k);
            std::string failed;
            for (int t = 1; t <= kMaxThreads; ++t) {
                set_threads(t);
                for (mm::Schedule s : kSchedules) {
                    for (int w : {1, 2, mm::kTileWidthBlocks, 7}) {
                        if (!identical(tiles(A, B, b, s, k, w), S)) failed += where(t, s, w);
                    }
                }
            }
            check(failed.empty(), std::string("[") + mm::kernel_name(k) + "] n = " + std::to_string(n) + ", b = " +
                                      std::to_string(b) +
                                      ": 1-12 thread, ca hai lich, tile rong 1/2/4/7 block giong tung bit multiply_blocked" +
                                      (failed.empty() ? "" : ", sai o (thread/lich/w):" + failed));
        }
        set_threads(kMaxThreads);
        const mm::CheckResult r = mm::compare_with_reference(A, B, tiles(A, B, b, mm::Schedule::Dynamic));
        check(r.ok, config(n, b, kMaxThreads, mm::Schedule::Dynamic) + ": dat nguong so voi tham chieu: " +
                        mm::describe(r));
    }
}

}  // namespace

int main() {
    try {
        test_tile_api();
        test_tile_exact();
        test_tile_uniform();
    } catch (const std::exception& e) {
        check(false, std::string("ngoai le khong mong doi: ") + e.what());
    }

    if (g_failures == 0) {
        std::printf("test_block_par: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_block_par: %d kiem tra that bai\n", g_failures);
    return 1;
}
