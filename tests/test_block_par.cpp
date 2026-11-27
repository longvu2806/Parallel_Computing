// Kiểm thử các bản nhân ma trận song song chia block (việc D7):
//   - tile (matmul/mm_par_2d.hpp, D5): đủ 1-12 thread, schedule static và dynamic.
//   - Cannon (matmul/mm_cannon.hpp, D6): lưới q = 1..4, 1-12 và 16 thread, barrier và nobarrier.
// So với hàm tham chiếu của Vũ (common/verify: reference_multiply, compare_with_reference)
// và với bản tuần tự mm::multiply_blocked (matmul/mm_kernel.hpp).
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include <omp.h>

#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/matrix.hpp"
#include "common/verify.hpp"
#include "matmul/mm_cannon.hpp"
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
// Gồm n không chia hết cho b, b > n, b = 1, số tile ít hơn số thread (n = 5, b = 8: 1 tile).
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
// với mọi số thread, mọi lịch, cả hai kernel, các bề rộng tile 1 (mặc định), 2, 4, 7 block;
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
                    for (int w : {1, 2, 4, 7}) {
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

// ---------------------------------------------------------------- Cannon

constexpr mm::CannonSync kSyncs[] = {mm::CannonSync::Barrier, mm::CannonSync::NoBarrier};
constexpr int kMaxGrid = 4;  // lưới 1x1 đến 4x4

// Số thread thử cho Cannon: 1-12 (đội thread khác q * q, ô chia vòng cho thread) và 16 (= 4 x 4).
std::vector<int> cannon_threads() {
    std::vector<int> ts;
    for (int t = 1; t <= kMaxThreads; ++t) ts.push_back(t);
    ts.push_back(16);
    return ts;
}

// C = A * B bằng multiply_cannon trên lưới q x q; C ban đầu chứa NaN như hàm tiles().
Matrix cannon(const Matrix& A, const Matrix& B, int b, int q, mm::CannonSync sync) {
    const int n = static_cast<int>(A.rows());
    Matrix C(A.rows(), A.rows());
    C.fill(NAN);
    mm::multiply_cannon(A.data(), B.data(), C.data(), n, b, q, sync);
    return C;
}

std::string where_cannon(int q, int t, mm::CannonSync sync) {
    return " q" + std::to_string(q) + "/" + std::to_string(t) + "/" + mm::cannon_sync_name(sync);
}

void test_cannon_api() {
    std::printf("-- Cannon: ten bien the, canh luoi, tham so sai\n");
    bool names = true;
    for (mm::CannonSync s : kSyncs) names = names && mm::parse_cannon_sync(mm::cannon_sync_name(s)) == s;
    int rejected = 0;
    for (const char* bad : {"", "Barrier", "simd", "barrier-w1", "no-barrier"}) {
        try { mm::parse_cannon_sync(bad); } catch (const std::invalid_argument&) { ++rejected; }
    }
    check(names && rejected == 5, "ten bien the: barrier, nobarrier doi qua lai dung, 5 ten sai bao loi");

    bool sides = mm::cannon_grid_side(1) == 1 && mm::cannon_grid_side(4) == 2 && mm::cannon_grid_side(9) == 3 &&
                 mm::cannon_grid_side(16) == 4 && mm::cannon_grid_side(1024) == 32;
    rejected = 0;
    for (int p : {0, -4, 2, 3, 5, 6, 8, 12, 1023}) {
        try { mm::cannon_grid_side(p); } catch (const std::invalid_argument&) { ++rejected; }
    }
    check(sides && rejected == 9, "canh luoi: 1/4/9/16/1024 thread -> q = 1/2/3/4/32; 0, -4, 2, 3, 5, 6, 8, 12, 1023 bao loi");

    // Lịch khối của Cannon: m = cannon_k_block(I, J, s, q) phải là hoán vị của 0..q-1 theo s (đủ số hạng),
    // theo J (mỗi khối A[I][m] chỉ một ô dùng ở bước s) và theo I (mỗi khối B[m][J] chỉ một ô dùng).
    // Một lịch đúng kết quả nhưng không phải Cannon (ví dụ m = s: cả hàng đọc chung một khối A) sẽ sai ở đây.
    std::string bad_q;
    for (int q = 1; q <= 6; ++q) {
        bool ok = true;
        for (int x = 0; x < q; ++x) {
            for (int y = 0; y < q; ++y) {
                std::vector<int> over_s(q, 0), over_j(q, 0), over_i(q, 0);
                for (int z = 0; z < q; ++z) {
                    ++over_s[mm::cannon_k_block(x, y, z, q)];  // ô (x, y), bước z
                    ++over_j[mm::cannon_k_block(x, z, y, q)];  // hàng x, bước y, cột z
                    ++over_i[mm::cannon_k_block(z, x, y, q)];  // cột x, bước y, hàng z
                }
                for (int m = 0; m < q; ++m) ok = ok && over_s[m] == 1 && over_j[m] == 1 && over_i[m] == 1;
            }
        }
        if (!ok) bad_q += " " + std::to_string(q);
    }
    check(bad_q.empty(), "lich khoi Cannon q = 1-6: du khoi k, moi buoc moi khoi A, B chi mot o dung" +
                             (bad_q.empty() ? "" : ", sai o q =" + bad_q));

    double c = 1.0;
    rejected = 0;
    try { mm::multiply_cannon(&c, &c, &c, 1, 0, 1); } catch (const std::invalid_argument&) { ++rejected; }
    try { mm::multiply_cannon(&c, &c, &c, 1, 1, 0); } catch (const std::invalid_argument&) { ++rejected; }
    try { mm::multiply_cannon(&c, &c, &c, -1, 1, 1); } catch (const std::invalid_argument&) { ++rejected; }
    check(rejected == 3, "b = 0, q = 0, n < 0 bao loi");

    c = 5.0;
    mm::multiply_cannon(nullptr, nullptr, &c, 0, 8, 2);
    check(c == 5.0, "n = 0: khong lam gi");
}

// Dữ liệu số nguyên nhỏ: mọi thứ tự cộng đều cho cùng kết quả, nên mọi lưới, mọi số thread và
// cả hai biến thể phải khớp tuyệt đối tham chiếu của Vũ.
// Gồm n < q (n = 2, q = 3, 4: có khối lớn rỗng), n không chia hết cho q hay b, b = 1, b > khối lớn.
void test_cannon_exact() {
    std::printf("-- Cannon: SmallInt, q = 1-%d, 1-%d va 16 thread, khop tuyet doi voi tham chieu\n", kMaxGrid,
                kMaxThreads);
    const int cases[][2] = {{1, 1}, {2, 8}, {5, 1}, {37, 8}, {64, 16}, {100, 48}, {130, 7}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 700 + n, A, B, Fill::SmallInt);
        const Matrix R = mm::reference_multiply(A, B);
        std::string failed;
        for (int t : cannon_threads()) {
            set_threads(t);
            for (int q = 1; q <= kMaxGrid; ++q) {
                for (mm::CannonSync s : kSyncs) {
                    if (!identical(cannon(A, B, b, q, s), R)) failed += where_cannon(q, t, s);
                }
            }
        }
        check(failed.empty(), "n = " + std::to_string(n) + ", b = " + std::to_string(b) +
                                  ": moi luoi, moi so thread, ca hai bien the khop tham chieu" +
                                  (failed.empty() ? "" : ", sai o (q/thread/bien the):" + failed));
    }
}

// Dữ liệu thực: thứ tự cộng chỉ phụ thuộc q và b, nên với mỗi q, mọi số thread và cả hai biến thể
// phải giống từng bit bản chạy 1 thread (cùng q). q = 1 phải giống từng bit multiply_blocked.
// Mọi q đạt ngưỡng sai số so với tham chiếu.
void test_cannon_uniform() {
    std::printf("-- Cannon: so thuc, giong tung bit giua cac so thread, q = 1 giong multiply_blocked\n");
    const int cases[][2] = {{97, 32}, {200, 48}, {257, 64}};
    for (const auto& nb : cases) {
        const int n = nb[0], b = nb[1];
        Matrix A, B;
        generate_matmul_input(static_cast<std::size_t>(n), 900 + n, A, B);
        Matrix S(A.rows(), A.rows());
        mm::multiply_blocked(A.data(), B.data(), S.data(), n, b);

        std::string failed, inexact;
        bool q1_same = false;
        for (int q = 1; q <= kMaxGrid; ++q) {
            set_threads(1);
            const Matrix one = cannon(A, B, b, q, mm::CannonSync::Barrier);
            if (q == 1) q1_same = identical(one, S);
            const mm::CheckResult r = mm::compare_with_reference(A, B, one);
            if (!r.ok) inexact += " q" + std::to_string(q) + ": " + mm::describe(r);
            for (int t : cannon_threads()) {
                set_threads(t);
                for (mm::CannonSync s : kSyncs) {
                    if (!identical(cannon(A, B, b, q, s), one)) failed += where_cannon(q, t, s);
                }
            }
        }
        const std::string nb_text = "n = " + std::to_string(n) + ", b = " + std::to_string(b);
        check(q1_same, nb_text + ": q = 1 giong tung bit multiply_blocked");
        check(failed.empty(), nb_text + ": moi q, moi so thread, ca hai bien the giong tung bit ban 1 thread" +
                                  (failed.empty() ? "" : ", sai o (q/thread/bien the):" + failed));
        check(inexact.empty(), nb_text + ": q = 1-4 dat nguong so voi tham chieu" + inexact);
    }
}

}  // namespace

int main() {
    try {
        test_tile_api();
        test_tile_exact();
        test_tile_uniform();
        test_cannon_api();
        test_cannon_exact();
        test_cannon_uniform();
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
