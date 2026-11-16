// Cài đặt các hàm trong matrix.hpp.

#include "common/matrix.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mm_malloc.h>   // _mm_malloc / _mm_free (std::aligned_alloc không có trên MinGW)
#include <new>
#include <random>
#include <stdexcept>
#include <utility>

Matrix::Matrix(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols) {
    if (rows == 0 || cols == 0) throw std::invalid_argument("Matrix: kich thuoc phai > 0");
    if (rows > std::numeric_limits<std::size_t>::max() / sizeof(double) / cols) {
        throw std::length_error("Matrix: kich thuoc qua lon");
    }
    data_ = static_cast<double*>(_mm_malloc(rows * cols * sizeof(double), kAlignment));
    if (data_ == nullptr) throw std::bad_alloc();
    std::fill_n(data_, rows * cols, 0.0);
}

Matrix::~Matrix() { _mm_free(data_); }   // _mm_free(nullptr) an toàn

Matrix::Matrix(Matrix&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      rows_(std::exchange(other.rows_, 0)),
      cols_(std::exchange(other.cols_, 0)) {}

Matrix& Matrix::operator=(Matrix&& other) noexcept {
    if (this != &other) {
        _mm_free(data_);
        data_ = std::exchange(other.data_, nullptr);
        rows_ = std::exchange(other.rows_, 0);
        cols_ = std::exchange(other.cols_, 0);
    }
    return *this;
}

Matrix Matrix::clone() const {
    if (empty()) return Matrix();
    Matrix copy(rows_, cols_);
    std::copy_n(data_, size(), copy.data_);
    return copy;
}

void Matrix::fill(double value) { std::fill_n(data_, size(), value); }

namespace {

// Lấy tiếp các phần tử của m từ dòng ngẫu nhiên rng (dùng chung cho fill_random và generate_*).
void fill_from(Matrix& m, std::mt19937_64& rng, Fill kind) {
    double* p = m.data();
    const std::size_t count = m.size();
    if (kind == Fill::Uniform) {
        // 53 bit cao -> số thực trong [0, 1) với bước 2^-53, rồi đưa về [-1, 1).
        // Mọi phép tính ở đây đều chính xác nên kết quả giống nhau trên mọi máy.
        for (std::size_t k = 0; k < count; ++k) {
            const double u = static_cast<double>(rng() >> 11) * 0x1.0p-53;
            p[k] = 2.0 * u - 1.0;
        }
    } else {
        // 32 bit cao lấy dư cho 9 -> {0..8} -> {-4..4}. Độ lệch phân phối không đáng kể.
        for (std::size_t k = 0; k < count; ++k) {
            p[k] = static_cast<double>(static_cast<int>((rng() >> 32) % 9) - 4);
        }
    }
}

}  // namespace

void fill_random(Matrix& m, std::uint64_t seed, Fill kind) {
    std::mt19937_64 rng(seed);
    fill_from(m, rng, kind);
}

void generate_dot_input(std::size_t n, std::uint64_t seed, Matrix& x, Matrix& y, Fill kind) {
    x = make_vector(n);
    y = make_vector(n);
    std::mt19937_64 rng(seed);
    fill_from(x, rng, kind);
    fill_from(y, rng, kind);
}

void generate_matmul_input(std::size_t n, std::uint64_t seed, Matrix& A, Matrix& B, Fill kind) {
    A = Matrix(n, n);
    B = Matrix(n, n);
    std::mt19937_64 rng(seed);
    fill_from(A, rng, kind);
    fill_from(B, rng, kind);
}
