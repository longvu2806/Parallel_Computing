// Kiểm thử nhân ma trận tuần tự: bản tham chiếu, hàm so sánh, Freivalds (common/verify)
// và 6 thứ tự vòng lặp (matmul/mm_seq.hpp).
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.

#include "common/matrix.hpp"
#include "common/verify.hpp"
#include "matmul/mm_seq.hpp"

#include <cmath>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

bool identical(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (a[k] != b[k]) return false;
    }
    return true;
}

Matrix multiply(mm::LoopOrder o, const Matrix& A, const Matrix& B) {
    Matrix C(A.rows(), A.rows());
    C.fill(-7.0);   // rác: các hàm phải ghi đè, không được dựa vào C = 0
    mm::multiply(o, A.data(), B.data(), C.data(), A.rows());
    return C;
}

// ---------------------------------------------------------------- bản tham chiếu và hàm so sánh

void test_reference() {
    std::printf("-- tham chieu va so sanh\n");
    Matrix A(2, 2), B(2, 2);
    A(0, 0) = 1; A(0, 1) = 2; A(1, 0) = 3; A(1, 1) = 4;
    B(0, 0) = 5; B(0, 1) = 6; B(1, 0) = 7; B(1, 1) = 8;
    const Matrix R = mm::reference_multiply(A, B);
    check(R(0, 0) == 19 && R(0, 1) == 22 && R(1, 0) == 43 && R(1, 1) == 50, "2 x 2: [1 2; 3 4] * [5 6; 7 8] = [19 22; 43 50]");

    // Nhân với ma trận đơn vị.
    Matrix X, Y;
    generate_matmul_input(9, 3, X, Y);
    Matrix I(9, 9);
    for (std::size_t i = 0; i < 9; ++i) I(i, i) = 1.0;
    check(identical(mm::reference_multiply(X, I), X) && identical(mm::reference_multiply(I, X), X), "A * I = I * A = A");

    // Dữ liệu thực: tham chiếu phải đạt chính nó; lệch một phần tử phải bị bắt và chỉ đúng vị trí.
    Matrix A2, B2;
    generate_matmul_input(40, 11, A2, B2);
    Matrix C2 = mm::reference_multiply(A2, B2);
    const mm::CheckResult self = mm::compare_with_reference(A2, B2, C2);
    check(self.ok && self.worst_ratio < 1.0, "tham chieu dat khi so voi chinh no: " + mm::describe(self));

    C2(17, 23) += 1e-6;
    const mm::CheckResult bad = mm::compare_with_reference(A2, B2, C2);
    check(!bad.ok && bad.i == 17 && bad.j == 23, "lech 1e-6 o mot phan tu bi bat dung vi tri: " + mm::describe(bad));

    // Sai số làm tròn cỡ vài ulp vẫn phải được chấp nhận.
    Matrix C3 = mm::reference_multiply(A2, B2);
    for (std::size_t k = 0; k < C3.size(); k += 7) C3[k] = std::nextafter(std::nextafter(C3[k], 1e300), 1e300);
    check(mm::compare_with_reference(A2, B2, C3).ok, "lech 2 ulp (sai so lam tron) duoc chap nhan");

    Matrix C4 = mm::reference_multiply(A2, B2);
    C4(0, 0) = NAN;
    check(!mm::compare_with_reference(A2, B2, C4).ok, "NaN trong C bi bat");

    bool threw = false;
    Matrix wrong(40, 41);
    try { mm::compare_with_reference(A2, B2, wrong); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "C sai kich thuoc bao loi");

    check(mm::checksum(R) == 19 + 22 + 43 + 50, "checksum la tong cac phan tu");
}

// ---------------------------------------------------------------- Freivalds

void test_freivalds() {
    std::printf("-- Freivalds\n");
    Matrix A, B;
    generate_matmul_input(300, 21, A, B);
    Matrix C = mm::reference_multiply(A, B);
    const mm::CheckResult good = mm::freivalds_check(A, B, C);
    check(good.ok, "C dung thi dat: " + mm::describe(good));

    Matrix C1 = C.clone();
    C1(123, 45) += 1e-6;
    const mm::CheckResult one = mm::freivalds_check(A, B, C1);
    check(!one.ok && one.i == 123, "lech 1e-6 o mot phan tu bi bat dung hang: " + mm::describe(one));

    // Lỗi cài đặt điển hình: thiếu số hạng k = n - 1 (vòng lặp chạy thiếu một lần).
    Matrix C2 = C.clone();
    for (std::size_t i = 0; i < 300; ++i) {
        for (std::size_t j = 0; j < 300; ++j) C2(i, j) -= A(i, 299) * B(299, j);
    }
    check(!mm::freivalds_check(A, B, C2).ok, "thieu so hang k = n - 1 bi bat");

    // Lỗi đổi chỗ hai hàng.
    Matrix C3 = C.clone();
    for (std::size_t j = 0; j < 300; ++j) std::swap(C3(5, j), C3(6, j));
    check(!mm::freivalds_check(A, B, C3).ok, "doi cho hai hang bi bat");

    // Kết quả tính theo thứ tự cộng khác (sai số làm tròn thật) vẫn đạt.
    Matrix C4(300, 300);
    mm::multiply(mm::LoopOrder::KJI, A.data(), B.data(), C4.data(), 300);
    check(mm::freivalds_check(A, B, C4).ok, "ket qua kji (lam tron double) dat Freivalds");

    // Dữ liệu SmallInt: chính xác tuyệt đối.
    Matrix S, T;
    generate_matmul_input(200, 4, S, T, Fill::SmallInt);
    Matrix U = mm::reference_multiply(S, T);
    check(mm::freivalds_check(S, T, U).ok, "SmallInt: C dung thi dat");
    U(199, 0) += 1.0;
    check(!mm::freivalds_check(S, T, U).ok, "SmallInt: lech 1 o phan tu cuoi bi bat");

    check(mm::verify_product(A, B, C).method == "reference", "verify_product: n = 300 so voi tham chieu");
    Matrix L(mm::kReferenceLimit + 1, mm::kReferenceLimit + 1);
    check(mm::verify_product(L, L, L).method == "freivalds" && mm::verify_product(L, L, L).ok,
          "verify_product: n > 512 dung Freivalds");
}

// ---------------------------------------------------------------- 6 thứ tự vòng lặp

void test_loop_orders() {
    std::printf("-- 6 thu tu vong lap\n");
    bool names = true;
    for (mm::LoopOrder o : mm::kAllLoopOrders) names = names && mm::parse_loop_order(mm::loop_order_name(o)) == o;
    bool threw = false;
    try { mm::parse_loop_order("ijj"); } catch (const std::invalid_argument&) { threw = true; }
    check(names && threw, "ten bien the: doi qua lai dung, ten sai bao loi");

    // SmallInt: mọi phép tính chính xác, nên phải khớp tuyệt đối với bản tham chiếu.
    // Gồm n = 1 và các n lẻ, không chia hết cho độ rộng vector.
    for (std::size_t n : {1, 2, 3, 7, 16, 33, 65, 100}) {
        Matrix A, B;
        generate_matmul_input(n, 100 + n, A, B, Fill::SmallInt);
        const Matrix R = mm::reference_multiply(A, B);
        std::string failed;
        for (mm::LoopOrder o : mm::kAllLoopOrders) {
            if (!identical(multiply(o, A, B), R)) failed += std::string(" ") + mm::loop_order_name(o);
        }
        check(failed.empty(), "SmallInt n = " + std::to_string(n) + ": 6 thu tu khop tuyet doi" +
                                  (failed.empty() ? "" : ", sai:" + failed));
    }

    // Dữ liệu thực: mọi thứ tự đạt ngưỡng sai số so với bản tham chiếu.
    for (std::size_t n : {31, 128, 200}) {
        Matrix A, B;
        generate_matmul_input(n, 7 * n, A, B);
        std::string failed;
        double worst = 0.0;
        for (mm::LoopOrder o : mm::kAllLoopOrders) {
            const mm::CheckResult r = mm::compare_with_reference(A, B, multiply(o, A, B));
            if (!r.ok) failed += std::string(" ") + mm::loop_order_name(o);
            worst = r.worst_ratio > worst ? r.worst_ratio : worst;
        }
        char ratio[64];
        std::snprintf(ratio, sizeof ratio, ", sai so lon nhat = %.3g lan nguong", worst);
        check(failed.empty(), "Uniform n = " + std::to_string(n) + ": 6 thu tu dat nguong" + ratio +
                                  (failed.empty() ? "" : ", sai:" + failed));
    }

    // Thông tin (không tính là lỗi): cả 6 thứ tự cộng theo k tăng dần nên thường khớp từng bit;
    // có thể khác nếu trình biên dịch dùng FMA ở vòng này mà không dùng ở vòng khác.
    Matrix A, B;
    generate_matmul_input(128, 5, A, B);
    const Matrix base = multiply(mm::LoopOrder::IJK, A, B);
    std::string same_bits = "ijk";
    for (mm::LoopOrder o : mm::kAllLoopOrders) {
        if (o != mm::LoopOrder::IJK && identical(multiply(o, A, B), base)) same_bits += std::string(" ") + mm::loop_order_name(o);
    }
    std::printf("[INFO] Uniform n = 128, cac thu tu khop tung bit voi ijk: %s\n", same_bits.c_str());
}

}  // namespace

int main() {
    try {
        test_reference();
        test_freivalds();
        test_loop_orders();
    } catch (const std::exception& e) {
        check(false, std::string("ngoai le khong mong doi: ") + e.what());
    }

    if (g_failures == 0) {
        std::printf("test_matmul: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_matmul: %d kiem tra that bai\n", g_failures);
    return 1;
}
