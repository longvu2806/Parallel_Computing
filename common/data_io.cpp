// Cài đặt các hàm trong data_io.hpp.

#include "common/data_io.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

// Đọc toàn bộ file vào bộ nhớ. std::string luôn kết thúc bằng '\0' nên strtod/strtoll
// không đọc vượt ra ngoài.
std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(path + ": khong mo duoc file");
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) throw std::runtime_error(path + ": khong doc duoc kich thuoc file");
    std::string text(static_cast<std::size_t>(size), '\0');
    in.seekg(0, std::ios::beg);
    if (size > 0 && !in.read(&text[0], size)) throw std::runtime_error(path + ": loi khi doc file");
    return text;
}

// Duyệt văn bản theo từng token cách nhau bởi khoảng trắng, báo lỗi kèm số dòng.
class Reader {
public:
    Reader(const std::string& path, const std::string& text)
        : path_(path), begin_(text.c_str()), p_(begin_), end_(begin_ + text.size()) {
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) p_ += 3;   // bỏ BOM UTF-8 nếu có
    }

    [[noreturn]] void fail(const std::string& message) const {
        long line = 1;
        for (const char* q = begin_; q < p_; ++q) line += (*q == '\n');
        throw std::runtime_error(path_ + ": dong " + std::to_string(line) + ": " + message);
    }

    void skip_space() {
        while (p_ < end_ && is_space(*p_)) ++p_;
    }

    bool at_end() {
        skip_space();
        return p_ >= end_;
    }

    // Số byte còn lại, dùng để chặn header khai n quá lớn so với file (tránh cấp phát vô ích).
    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }

    std::string word() {
        skip_space();
        const char* start = p_;
        while (p_ < end_ && !is_space(*p_)) ++p_;
        return std::string(start, p_);
    }

    long long positive_integer(const char* what) {
        skip_space();
        const std::string token = peek_token();
        char* stop = nullptr;
        errno = 0;
        const long long value = std::strtoll(p_, &stop, 10);
        if (stop == p_ || !token_ends_at(stop) || errno == ERANGE) fail(std::string(what) + " khong phai so nguyen: " + token);
        if (value <= 0) fail(std::string(what) + " phai > 0: " + token);
        p_ = stop;
        return value;
    }

    // Đọc số thực thứ index (đếm từ 1) trong tổng số need.
    double number(std::size_t index, std::size_t need) {
        skip_space();
        if (p_ >= end_) {
            fail("thieu so: can " + std::to_string(need) + " so sau dong dau, moi co " +
                 std::to_string(index - 1));
        }
        char* stop = nullptr;
        const double value = std::strtod(p_, &stop);
        if (stop == p_ || !token_ends_at(stop)) {
            fail("so thu " + std::to_string(index) + " khong hop le: " + peek_token());
        }
        if (!std::isfinite(value)) fail("so thu " + std::to_string(index) + " khong huu han: " + peek_token());
        p_ = stop;
        return value;
    }

private:
    bool token_ends_at(const char* q) const { return q >= end_ || is_space(*q); }

    std::string peek_token() const {
        const char* q = p_;
        while (q < end_ && !is_space(*q) && q - p_ < 40) ++q;
        return std::string(p_, q);
    }

    std::string path_;
    const char* begin_;
    const char* p_;
    const char* end_;
};

// Đọc dòng đầu "<keyword> <n>" và kiểm tra file có thể chứa đủ count_per_n(n) số.
long long read_header(Reader& r, const char* keyword, bool square) {
    const std::string word = r.word();
    if (word != keyword) {
        r.fail(std::string("dong dau phai la '") + keyword + " <n>', dang la '" + word.substr(0, 40) + "'");
    }
    const long long n = r.positive_integer("n");

    // Mỗi số chiếm ít nhất 1 ký tự và 1 khoảng trắng phân cách.
    const unsigned long long max_numbers = (r.remaining() + 1) / 2;
    const unsigned long long un = static_cast<unsigned long long>(n);
    const bool too_big = square ? (un > max_numbers / 2 / un) : (un > max_numbers / 2);
    if (too_big) r.fail("file qua ngan so voi n = " + std::to_string(n) + " (thieu so)");
    return n;
}

void read_numbers(Reader& r, Matrix& m, std::size_t first_index, std::size_t need) {
    double* p = m.data();
    for (std::size_t k = 0; k < m.size(); ++k) p[k] = r.number(first_index + k, need);
}

void expect_end(Reader& r, std::size_t need) {
    if (!r.at_end()) r.fail("thua du lieu sau " + std::to_string(need) + " so");
}

// Mở file để ghi; đóng và kiểm tra lỗi ghi qua finish().
std::FILE* open_for_write(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) throw std::runtime_error(path + ": khong mo duoc file de ghi");
    return f;
}

void finish(std::FILE* f, const std::string& path) {
    const bool had_error = std::ferror(f) != 0;
    if (std::fclose(f) != 0 || had_error) throw std::runtime_error(path + ": loi khi ghi file");
}

void check_n_matches(const Options& opt, long long n) {
    if (opt.n != 0 && opt.n != n) {
        throw std::runtime_error("--n " + std::to_string(opt.n) + " khac n = " + std::to_string(n) +
                                 " trong file " + opt.input);
    }
}

}  // namespace

void read_dot_input(const std::string& path, Matrix& x, Matrix& y) {
    const std::string text = read_file(path);
    Reader r(path, text);
    const long long n = read_header(r, "dot", false);
    const std::size_t un = static_cast<std::size_t>(n);
    const std::size_t need = 2 * un;
    Matrix nx = make_vector(un), ny = make_vector(un);
    read_numbers(r, nx, 1, need);
    read_numbers(r, ny, un + 1, need);
    expect_end(r, need);
    x = std::move(nx);   // chỉ gán khi đọc thành công, lỗi giữa chừng không làm hỏng x, y cũ
    y = std::move(ny);
}

void read_matmul_input(const std::string& path, Matrix& A, Matrix& B) {
    const std::string text = read_file(path);
    Reader r(path, text);
    const long long n = read_header(r, "matmul", true);
    const std::size_t un = static_cast<std::size_t>(n);
    const std::size_t need = 2 * un * un;
    Matrix nA(un, un), nB(un, un);
    read_numbers(r, nA, 1, need);
    read_numbers(r, nB, un * un + 1, need);
    expect_end(r, need);
    A = std::move(nA);
    B = std::move(nB);
}

void write_dot_result(const std::string& path, double value) {
    std::FILE* f = open_for_write(path);
    std::fprintf(f, "dot_result %.17g\n", value);
    finish(f, path);
}

void write_matrix_result(const std::string& path, const Matrix& C) {
    if (C.rows() != C.cols()) throw std::invalid_argument("write_matrix_result: C phai la ma tran vuong");
    std::FILE* f = open_for_write(path);
    const std::size_t n = C.rows();
    std::fprintf(f, "matrix %zu\n", n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) std::fprintf(f, j ? " %.17g" : "%.17g", C(i, j));
        std::fputc('\n', f);
    }
    finish(f, path);
}

long long load_dot_input(const Options& opt, Matrix& x, Matrix& y, pc::PhaseTimer& timer) {
    if (!opt.input.empty()) {
        {
            auto scope = timer.measure(pc::Phase::IO);
            read_dot_input(opt.input, x, y);
        }
        const long long n = static_cast<long long>(x.size());
        check_n_matches(opt, n);
        return n;
    }
    if (opt.n <= 0) throw std::runtime_error("can --n hoac --input");
    auto scope = timer.measure(pc::Phase::Init);
    generate_dot_input(static_cast<std::size_t>(opt.n), opt.seed, x, y);
    return opt.n;
}

long long load_matmul_input(const Options& opt, Matrix& A, Matrix& B, pc::PhaseTimer& timer) {
    if (!opt.input.empty()) {
        {
            auto scope = timer.measure(pc::Phase::IO);
            read_matmul_input(opt.input, A, B);
        }
        const long long n = static_cast<long long>(A.rows());
        check_n_matches(opt, n);
        return n;
    }
    if (opt.n <= 0) throw std::runtime_error("can --n hoac --input");
    auto scope = timer.measure(pc::Phase::Init);
    generate_matmul_input(static_cast<std::size_t>(opt.n), opt.seed, A, B);
    return opt.n;
}
