#pragma once

// Kiểm tra kết quả nhân ma trận vuông C = A * B, dùng cho --verify của mọi bản nhân ma trận
// và cho các test trong tests/.
//
// Hai cách kiểm tra:
//   compare_with_reference  O(n^3): nhân lại bằng bản tham chiếu (cộng dồn long double),
//                           so từng phần tử với ngưỡng sai số làm tròn.
//   freivalds_check         O(n^2): chọn vector ngẫu nhiên r, so A(Br) với Cr.
//                           Dùng cho n lớn, khi nhân lại O(n^3) quá chậm.
// verify_product tự chọn một trong hai theo n.
//
// Ngưỡng sai số: mọi cách cộng n tích theo thứ tự bất kỳ (kể cả song song, có FMA) đều cho
//   |C_ij - (AB)_ij| <= gamma_n * (|A||B|)_ij,  gamma_n = n*u / (1 - n*u),  u = 2^-53
// (Higham, Accuracy and Stability of Numerical Algorithms, mục 3.1). Ngưỡng dùng ở đây là
// 2 * gamma_(n+1) để trừ hao sai số của chính bản tham chiếu. Với dữ liệu Fill::SmallInt
// mọi phép tính đều chính xác, nên kết quả đúng phải khớp tuyệt đối.

#include <cstddef>
#include <cstdint>
#include <string>

#include "common/matrix.hpp"

namespace mm {

struct CheckResult {
    bool ok = true;
    std::string method;        // "reference" hoặc "freivalds"
    // Vị trí lệch nhiều nhất so với ngưỡng (freivalds: chỉ có hàng i, j = 0).
    std::size_t i = 0, j = 0;
    double got = 0.0;          // giá trị của C (freivalds: (Cr)_i)
    double want = 0.0;         // giá trị đúng (freivalds: (A(Br))_i)
    double bound = 0.0;        // ngưỡng sai số cho phép tại vị trí đó
    double worst_ratio = 0.0;  // max |got - want| / bound; ok khi <= 1
};

// Nhân tham chiếu: vòng lặp đơn giản nhất, cộng dồn bằng long double. Chỉ dùng cho n vừa phải.
Matrix reference_multiply(const Matrix& A, const Matrix& B);

// So C với tích tham chiếu, từng phần tử, theo ngưỡng ở đầu file. O(n^3).
CheckResult compare_with_reference(const Matrix& A, const Matrix& B, const Matrix& C);

// Kiểm tra Freivalds, lặp trials lần với các vector r khác nhau sinh từ seed. O(n^2) mỗi lần.
// r_j có độ lớn trong [1, 2) và dấu ngẫu nhiên, nên một phần tử sai đơn lẻ không bị nhân với số
// gần 0. Ngưỡng tính theo hàng nên thô hơn compare_with_reference, nhưng vẫn nhỏ hơn rất nhiều
// so với sai lệch do lỗi cài đặt (thiếu một số hạng, sai chỉ số, ...).
CheckResult freivalds_check(const Matrix& A, const Matrix& B, const Matrix& C,
                            std::uint64_t seed = 12345, int trials = 2);

// n <= reference_limit thì so với bản tham chiếu, lớn hơn thì dùng Freivalds.
constexpr std::size_t kReferenceLimit = 512;
CheckResult verify_product(const Matrix& A, const Matrix& B, const Matrix& C);

// Một dòng mô tả kết quả, để in ra stderr khi --verify thất bại.
std::string describe(const CheckResult& r);

// Tổng mọi phần tử của C theo thứ tự bộ nhớ: cột check trong CSV của bài nhân ma trận.
// Mọi chương trình dùng chung hàm này, nên cùng C thì cùng check.
double checksum(const Matrix& C);

}  // namespace mm
