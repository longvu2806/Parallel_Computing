#pragma once

// Ma trận / vector số thực double dùng chung cho cả nhóm.
//
// - Lưu liên tiếp theo hàng (row-major): phần tử (i, j) nằm ở data()[i * cols() + j].
//   Với ma trận vuông n x n, data() và n truyền thẳng được cho block_multiply(A, B, C, n, ...).
// - Vùng nhớ căn lề 64 byte (một cache line, đủ cho vector AVX-512). Chỉ phần tử đầu được
//   bảo đảm căn lề; đầu mỗi hàng chỉ căn lề khi cols() chia hết cho 8.
// - Vector độ dài n là Matrix 1 x n (xem make_vector).
// - Matrix sở hữu vùng nhớ (RAII): không sao chép ngầm được, để tránh vô tình chép hàng trăm MB;
//   chỉ di chuyển (move). Cần bản sao thật thì gọi clone().

#include <cstddef>
#include <cstdint>

class Matrix {
public:
    static constexpr std::size_t kAlignment = 64;

    Matrix() = default;                           // rỗng, chưa cấp phát
    Matrix(std::size_t rows, std::size_t cols);   // cấp phát và gán toàn bộ bằng 0
    ~Matrix();

    Matrix(const Matrix&) = delete;
    Matrix& operator=(const Matrix&) = delete;
    Matrix(Matrix&& other) noexcept;
    Matrix& operator=(Matrix&& other) noexcept;

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    std::size_t size() const { return rows_ * cols_; }
    bool empty() const { return data_ == nullptr; }

    double* data() { return data_; }
    const double* data() const { return data_; }

    // Truy cập theo (hàng, cột). Không kiểm tra biên, để không làm chậm vòng lặp tính toán.
    double& operator()(std::size_t i, std::size_t j) { return data_[i * cols_ + j]; }
    const double& operator()(std::size_t i, std::size_t j) const { return data_[i * cols_ + j]; }

    // Truy cập theo chỉ số phẳng, tiện cho vector.
    double& operator[](std::size_t k) { return data_[k]; }
    const double& operator[](std::size_t k) const { return data_[k]; }

    Matrix clone() const;          // bản sao độc lập
    void fill(double value);       // gán mọi phần tử bằng value

private:
    double* data_ = nullptr;
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
};

inline Matrix make_vector(std::size_t n) { return Matrix(1, n); }

// Kiểu dữ liệu ngẫu nhiên:
//   Uniform  - số thực phân bố đều trong [-1, 1), dùng khi đo hiệu năng.
//   SmallInt - số nguyên trong [-4, 4]. Mọi tích và tổng đều là số nguyên nhỏ hơn 2^53 nên
//              được tính chính xác tuyệt đối theo mọi thứ tự cộng: kết quả tuần tự và song song
//              phải khớp từng bit. Dùng cho test đúng đắn.
enum class Fill { Uniform, SmallInt };

// Điền m bằng dãy ngẫu nhiên mt19937_64(seed), theo thứ tự phần tử trong bộ nhớ.
// Cùng seed luôn cho cùng dữ liệu, trên mọi máy và mọi trình biên dịch (không dùng
// std::uniform_real_distribution vì kết quả của nó phụ thuộc thư viện chuẩn).
void fill_random(Matrix& m, std::uint64_t seed, Fill kind = Fill::Uniform);

// Sinh dữ liệu đầu vào chuẩn của hai bài toán từ một seed.
// Dùng MỘT dòng ngẫu nhiên: sinh hết x rồi đến y (hoặc hết A rồi đến B), cùng thứ tự với
// định dạng file chung. Mọi chương trình gọi hai hàm này nên với cùng --n, --seed sẽ
// nhận đúng cùng dữ liệu, và cột check trong CSV so sánh được giữa các phiên bản.
void generate_dot_input(std::size_t n, std::uint64_t seed, Matrix& x, Matrix& y,
                        Fill kind = Fill::Uniform);
void generate_matmul_input(std::size_t n, std::uint64_t seed, Matrix& A, Matrix& B,
                           Fill kind = Fill::Uniform);
