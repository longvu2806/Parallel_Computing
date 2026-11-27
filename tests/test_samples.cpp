// Kiểm thử bằng dữ liệu mẫu có đáp án biết trước: mọi file data/sample/<tên>.txt được tính bằng
// các kernel của cả nhóm và so khớp TUYỆT ĐỐI với data/sample/expected/<tên>.txt.
// Mẫu chỉ chứa số nguyên nhỏ nên mọi phép tính double đều chính xác (xem scripts/make_samples.py).
// Chạy từ gốc repo (make test). Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include "common/data_io.hpp"
#include "common/matrix.hpp"
#include "common/verify.hpp"
#include "dot_product/dot_kernel.hpp"
#include "matmul/mm_cannon.hpp"
#include "matmul/mm_kernel.hpp"
#include "matmul/mm_par_1d.hpp"
#include "matmul/mm_par_2d.hpp"
#include "matmul/mm_seq.hpp"

#include <omp.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::string first_word(const fs::path& path) {
    std::ifstream in(path);
    std::string word;
    in >> word;
    return word;
}

// Đáp án theo định dạng kết quả chung: "dot_result <v>" hoặc "matrix <n>" + n*n số.
struct Expected {
    bool is_dot = false;
    double value = 0.0;
    Matrix C;
};

Expected read_expected(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error(path.string() + ": khong mo duoc file dap an");
    Expected e;
    std::string word;
    in >> word;
    if (word == "dot_result") {
        e.is_dot = true;
        if (!(in >> e.value)) throw std::runtime_error(path.string() + ": thieu gia tri dot_result");
    } else if (word == "matrix") {
        std::size_t n = 0;
        if (!(in >> n) || n == 0) throw std::runtime_error(path.string() + ": n khong hop le");
        e.C = Matrix(n, n);
        for (std::size_t k = 0; k < n * n; ++k) {
            if (!(in >> e.C[k])) throw std::runtime_error(path.string() + ": thieu so trong ma tran dap an");
        }
    } else {
        throw std::runtime_error(path.string() + ": dong dau phai la dot_result hoac matrix");
    }
    double extra;
    if (in >> extra) throw std::runtime_error(path.string() + ": thua du lieu");
    return e;
}

bool identical(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (a[k] != b[k]) return false;
    }
    return true;
}

void check_dot(const std::string& name, const fs::path& input, const Expected& e) {
    if (!e.is_dot) {
        check(false, name + ": dap an khong phai dot_result");
        return;
    }
    Matrix x, y;
    read_dot_input(input.string(), x, y);
    const long long n = static_cast<long long>(x.size());

    std::string failed;
    if (dot::seq_plain(x.data(), y.data(), n) != e.value) failed += " seq_plain";
    if (dot::seq_simd(x.data(), y.data(), n) != e.value) failed += " seq_simd";
    for (int threads : {1, 2, 4}) {
        omp_set_num_threads(threads);
        if (dot::omp_plain(x.data(), y.data(), n) != e.value) failed += " omp_plain/" + std::to_string(threads);
        if (dot::omp_simd(x.data(), y.data(), n) != e.value) failed += " omp_simd/" + std::to_string(threads);
    }
    check(failed.empty(), name + " (n = " + std::to_string(n) + "): 6 bien the tich vo huong khop dap an" +
                              (failed.empty() ? "" : ", sai:" + failed));
}

void check_matmul(const std::string& name, const fs::path& input, const Expected& e) {
    if (e.is_dot) {
        check(false, name + ": dap an khong phai matrix");
        return;
    }
    Matrix A, B;
    read_matmul_input(input.string(), A, B);
    const std::size_t n = A.rows();
    if (e.C.rows() != n) {
        check(false, name + ": dap an co n = " + std::to_string(e.C.rows()) + ", du lieu co n = " + std::to_string(n));
        return;
    }

    std::string failed;
    if (!identical(mm::reference_multiply(A, B), e.C)) failed += " tham_chieu";

    Matrix C(n, n);
    for (mm::LoopOrder o : mm::kAllLoopOrders) {
        C.fill(-1.0);
        mm::multiply(o, A.data(), B.data(), C.data(), n);
        if (!identical(C, e.C)) failed += std::string(" ") + mm::loop_order_name(o);
    }
    for (int b : {1, 4, 8, 64}) {
        C.fill(-1.0);
        mm::multiply_blocked(A.data(), B.data(), C.data(), static_cast<int>(n), b);
        if (!identical(C, e.C)) failed += " block/b=" + std::to_string(b);
    }
    // Bản song song: 1D theo dải hàng (Vũ) và tile 2D (Đức), b = 8, 1/4/12 thread, cả hai lịch.
    for (int threads : {1, 4, 12}) {
        omp_set_dynamic(0);
        omp_set_num_threads(threads);
        for (mm::Schedule s : {mm::Schedule::Static, mm::Schedule::Dynamic}) {
            const std::string tag = std::to_string(threads) + "/" + mm::schedule_name(s);
            C.fill(-1.0);
            mm::multiply_rows(A.data(), B.data(), C.data(), static_cast<int>(n), 8, s);
            if (!identical(C, e.C)) failed += " 1d/" + tag;
            C.fill(-1.0);
            mm::multiply_tiles(A.data(), B.data(), C.data(), static_cast<int>(n), 8, s);
            if (!identical(C, e.C)) failed += " tile/" + tag;
        }
    }
    // Cannon (Đức): lưới 1x1, 2x2, 3x3 với đúng q * q thread, b = 8, có và không có barrier.
    for (int q = 1; q <= 3; ++q) {
        omp_set_dynamic(0);
        omp_set_num_threads(q * q);
        for (mm::CannonSync s : {mm::CannonSync::Barrier, mm::CannonSync::NoBarrier}) {
            C.fill(-1.0);
            mm::multiply_cannon(A.data(), B.data(), C.data(), static_cast<int>(n), 8, q, s);
            if (!identical(C, e.C)) failed += " cannon/" + std::to_string(q * q) + "/" + mm::cannon_sync_name(s);
        }
    }
    check(failed.empty(), name + " (n = " + std::to_string(n) +
                              "): tham chieu, 6 thu tu, block b = 1/4/8/64, 1D va tile 1/4/12 thread, "
                              "Cannon 1/4/9 thread khop dap an" +
                              (failed.empty() ? "" : ", sai:" + failed));
}

}  // namespace

int main() {
    const fs::path sample_dir = "data/sample";
    const fs::path expected_dir = sample_dir / "expected";
    if (!fs::is_directory(sample_dir) || !fs::is_directory(expected_dir)) {
        check(false, "khong thay data/sample va data/sample/expected (phai chay tu goc repo, vd make test)");
        std::printf("test_samples: %d kiem tra that bai\n", g_failures);
        return 1;
    }

    std::set<std::string> inputs, answers;
    for (const auto& entry : fs::directory_iterator(sample_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") inputs.insert(entry.path().filename().string());
    }
    for (const auto& entry : fs::directory_iterator(expected_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".txt") answers.insert(entry.path().filename().string());
    }
    check(!inputs.empty(), "co " + std::to_string(inputs.size()) + " file mau trong data/sample");
    std::string unpaired;
    for (const std::string& f : inputs) {
        if (!answers.count(f)) unpaired += " " + f + "(thieu dap an)";
    }
    for (const std::string& f : answers) {
        if (!inputs.count(f)) unpaired += " " + f + "(dap an khong co mau)";
    }
    check(unpaired.empty(), "moi mau co dung mot file dap an" + (unpaired.empty() ? "" : ":" + unpaired));

    for (const std::string& f : inputs) {
        if (!answers.count(f)) continue;
        const fs::path input = sample_dir / f;
        try {
            const Expected e = read_expected(expected_dir / f);
            const std::string kind = first_word(input);
            if (kind == "dot") check_dot(f, input, e);
            else if (kind == "matmul") check_matmul(f, input, e);
            else check(false, f + ": dong dau phai la dot hoac matmul");
        } catch (const std::exception& ex) {
            check(false, f + ": " + ex.what());
        }
    }

    if (g_failures == 0) {
        std::printf("test_samples: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_samples: %d kiem tra that bai\n", g_failures);
    return 1;
}
