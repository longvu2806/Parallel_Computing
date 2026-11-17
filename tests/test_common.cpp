// Kiểm thử phần dùng chung: common/cli.hpp, common/matrix, common/data_io, common/csv_log.
// Trả mã thoát 0 nếu mọi kiểm tra đạt, 1 nếu có lỗi.
// File tạm được tạo trong thư mục tạm của hệ thống và xóa khi kết thúc.

#include "common/cli.hpp"
#include "common/csv_log.hpp"
#include "common/data_io.hpp"
#include "common/matrix.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
fs::path g_dir;   // thư mục tạm riêng của lần chạy test

void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", what.c_str());
    if (!ok) ++g_failures;
}

std::string tmp(const char* name) { return (g_dir / name).string(); }

void put(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string get(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

bool same(const Matrix& a, const Matrix& b) {
    if (a.rows() != b.rows() || a.cols() != b.cols()) return false;
    for (std::size_t k = 0; k < a.size(); ++k) {
        if (a[k] != b[k]) return false;
    }
    return true;
}

// ---------------------------------------------------------------- cli

Options parse(std::initializer_list<const char*> args) {
    std::vector<std::string> store = {"prog"};
    for (const char* a : args) store.emplace_back(a);
    std::vector<char*> argv;
    for (std::string& s : store) argv.push_back(&s[0]);
    return parse_options(static_cast<int>(argv.size()), argv.data());
}

// Trả về thông báo lỗi; chuỗi rỗng nghĩa là không có lỗi.
std::string parse_error(std::initializer_list<const char*> args) {
    try {
        parse(args);
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "";
}

void test_cli() {
    std::printf("-- cli\n");
    const Options d = parse({"--n", "100"});
    check(d.n == 100 && d.seed == 42 && d.b == 64 && d.threads == 1 && d.schedule == "static" &&
              !d.pin && d.run == 0 && !d.verify && d.input.empty() && d.variant.empty(),
          "gia tri mac dinh");

    const Options o = parse({"--n", "512", "--seed", "7", "--b", "32", "--threads", "6", "--variant", "ikj",
                             "--schedule", "dynamic", "--pin", "1", "--run", "3", "--csv", "r.csv",
                             "--output", "out.txt", "--verify"});
    check(o.n == 512 && o.seed == 7 && o.b == 32 && o.threads == 6 && o.variant == "ikj" &&
              o.schedule == "dynamic" && o.pin && o.run == 3 && o.csv == "r.csv" && o.output == "out.txt" && o.verify,
          "doc du moi tham so");
    check(parse({"--input", "a.txt"}).input == "a.txt", "--input thay cho --n");
    check(parse({"--help"}).help && parse({"-h"}).help, "--help khong can --n");

    check(contains(parse_error({}), "can --n"), "thieu ca --n va --input bao loi");
    check(contains(parse_error({"--n", "10", "--foo", "1"}), "khong ho tro"), "tham so la bao loi");
    check(contains(parse_error({"--n"}), "thieu gia tri"), "thieu gia tri bao loi");
    check(!parse_error({"--n", "0"}).empty(), "--n 0 bao loi");
    check(!parse_error({"--n", "12a"}).empty(), "--n 12a bao loi");
    check(!parse_error({"--n", "99999999999999999999"}).empty(), "--n tran so bao loi");
    check(!parse_error({"--n", "10", "--seed", "-1"}).empty(), "seed am bao loi");
    check(!parse_error({"--n", "10", "--threads", "0"}).empty(), "--threads 0 bao loi");
    check(!parse_error({"--n", "10", "--schedule", "guided"}).empty(), "--schedule la bao loi");
    check(!parse_error({"--n", "10", "--pin", "2"}).empty(), "--pin 2 bao loi");
}

// ---------------------------------------------------------------- matrix

void test_matrix() {
    std::printf("-- matrix\n");
    Matrix m(3, 5);
    bool zero = true;
    for (std::size_t k = 0; k < m.size(); ++k) zero = zero && m[k] == 0.0;
    check(m.rows() == 3 && m.cols() == 5 && m.size() == 15 && zero, "cap phat 3 x 5, moi phan tu bang 0");
    m(2, 4) = 7.0;
    check(m[14] == 7.0, "(i, j) nam o data()[i * cols + j]");

    bool aligned = true;
    for (std::size_t n = 1; n <= 100; n += 3) {
        const Matrix v = make_vector(n);
        aligned = aligned && reinterpret_cast<std::uintptr_t>(v.data()) % Matrix::kAlignment == 0;
    }
    check(aligned, "vung nho can le 64 byte voi nhieu kich thuoc");

    bool threw = false;
    try { Matrix bad(0, 4); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "kich thuoc 0 bao loi");
    threw = false;
    try { Matrix bad(SIZE_MAX / 4, 4); } catch (const std::length_error&) { threw = true; }
    check(threw, "kich thuoc tran size_t bao loi");

    Matrix a(4, 4);
    a.fill(2.0);
    const double* p = a.data();
    Matrix b(std::move(a));
    check(b.data() == p && a.empty() && b(3, 3) == 2.0, "move chuyen vung nho, ban cu thanh rong");
    Matrix c = b.clone();
    c[0] = 99.0;
    check(c.data() != b.data() && b[0] == 2.0, "clone tao ban sao doc lap");

    Matrix r1(64, 64), r2(64, 64), r3(64, 64);
    fill_random(r1, 42);
    fill_random(r2, 42);
    fill_random(r3, 43);
    check(same(r1, r2) && !same(r1, r3), "cung seed cung du lieu, khac seed khac du lieu");
    double lo = 1.0, hi = -1.0;
    for (std::size_t k = 0; k < r1.size(); ++k) {
        lo = r1[k] < lo ? r1[k] : lo;
        hi = r1[k] > hi ? r1[k] : hi;
    }
    check(lo >= -1.0 && hi < 1.0, "Uniform nam trong [-1, 1)");

    Matrix s(100, 100);
    fill_random(s, 1, Fill::SmallInt);
    bool ints = true;
    int seen[9] = {0};
    for (std::size_t k = 0; k < s.size(); ++k) {
        const double v = s[k];
        const bool ok = v >= -4.0 && v <= 4.0 && v == static_cast<double>(static_cast<int>(v));
        ints = ints && ok;
        if (ok) ++seen[static_cast<int>(v) + 4];
    }
    bool all_values = true;
    for (int count : seen) all_values = all_values && count > 0;
    check(ints && all_values, "SmallInt la so nguyen trong [-4, 4], du 9 gia tri");

    // SmallInt: cộng theo thứ tự xuôi và ngược phải khớp từng bit (không có sai số làm tròn).
    Matrix x, y;
    generate_dot_input(100000, 5, x, y, Fill::SmallInt);
    double forward = 0.0, backward = 0.0;
    for (std::size_t k = 0; k < x.size(); ++k) forward += x[k] * y[k];
    for (std::size_t k = x.size(); k-- > 0;) backward += x[k] * y[k];
    check(forward == backward, "SmallInt: tich vo huong khong phu thuoc thu tu cong");

    // generate_*: một dòng ngẫu nhiên, sinh x rồi đến y.
    Matrix gx, gy, both(1, 2000);
    generate_dot_input(1000, 42, gx, gy);
    fill_random(both, 42);
    bool stream = gx.cols() == 1000 && gy.cols() == 1000;
    for (std::size_t k = 0; k < 1000 && stream; ++k) stream = gx[k] == both[k] && gy[k] == both[1000 + k];
    check(stream, "generate_dot_input: y noi tiep x tren cung dong ngau nhien");

    Matrix A, B, A2(50, 50);
    generate_matmul_input(50, 9, A, B, Fill::SmallInt);
    fill_random(A2, 9, Fill::SmallInt);
    check(A.rows() == 50 && B.rows() == 50 && same(A, A2) && !same(A, B), "generate_matmul_input: A truoc, B sau");
}

// ---------------------------------------------------------------- data_io

std::string dot_error(const std::string& text) {
    const std::string path = tmp("bad.txt");
    put(path, text);
    Matrix x, y;
    try {
        read_dot_input(path, x, y);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

std::string matmul_error(const std::string& text) {
    const std::string path = tmp("bad.txt");
    put(path, text);
    Matrix A, B;
    try {
        read_matmul_input(path, A, B);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

void test_data_io() {
    std::printf("-- data_io\n");
    const std::string in = tmp("in.txt");

    put(in, "dot 4\n1 2 3 4\n5 6 7 8\n");
    Matrix x, y;
    read_dot_input(in, x, y);
    check(x.cols() == 4 && y.cols() == 4 && x[0] == 1 && x[3] == 4 && y[0] == 5 && y[3] == 8, "doc file dot");

    put(in, "\xEF\xBB\xBFmatmul 2\r\n1 2\r\n3 4\r\n5 6\r\n7 8\r\n");
    Matrix A, B;
    read_matmul_input(in, A, B);
    check(A.rows() == 2 && A(0, 1) == 2 && A(1, 0) == 3 && B(0, 0) == 5 && B(1, 1) == 8,
          "doc file matmul co BOM va xuong dong CRLF");

    put(in, "dot 2  -1.5e-3 +2\t\n0.25\n\n-0 \n");
    read_dot_input(in, x, y);
    check(x[0] == -1.5e-3 && x[1] == 2 && y[0] == 0.25 && y[1] == 0, "khoang trang bat ky, so co dau va dang mu");

    const std::string missing = dot_error("dot 3\n1 2 3\n4 5\n");
    check(contains(missing, "thieu so") && contains(missing, "dong"), "thieu so bao loi kem so dong: " + missing);
    const std::string extra = dot_error("dot 2\n1 2\n3 4\n5\n");
    check(contains(extra, "thua du lieu") && contains(extra, "dong 4"), "thua so bao loi: " + extra);
    const std::string word = dot_error("dot 2\n1 2\n3 4x\n");
    check(contains(word, "so thu 4") && contains(word, "dong 3"), "so khong hop le bao loi: " + word);
    check(contains(dot_error("dot 2\n1 nan\n3 4\n"), "khong huu han") &&
              contains(dot_error("dot 2\n1 inf\n3 4\n"), "khong huu han") &&
              contains(dot_error("dot 2\n1 1e999\n3 4\n"), "khong huu han"),
          "NaN, Inf, so tran bao loi");
    check(contains(dot_error("matmul 1\n1\n2\n"), "dong dau") && contains(matmul_error("dot 1\n1\n2\n"), "dong dau") &&
              contains(dot_error(""), "dong dau"),
          "sai tu khoa dau file hoac file rong bao loi");
    check(contains(dot_error("dot 0\n"), "> 0") && contains(dot_error("dot -3\n1 2 3 4 5 6\n"), "> 0") &&
              contains(dot_error("dot 2.5\n1 2 3 4\n"), "so nguyen"),
          "n bang 0, am, khong nguyen bao loi");
    check(contains(matmul_error("matmul 100000\n1 2 3\n"), "qua ngan"),
          "header khai n qua lon so voi file bao loi truoc khi cap phat");

    Matrix ox = make_vector(3), oy = make_vector(3);
    ox[0] = 42.0;
    put(in, "dot 2\n1 2\n3 z\n");
    try { read_dot_input(in, ox, oy); } catch (const std::runtime_error&) {}
    check(ox.cols() == 3 && ox[0] == 42.0, "doc loi giua chung khong lam hong du lieu cu");

    bool threw = false;
    try { read_dot_input(tmp("khong_co.txt"), x, y); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "khong co file bao loi");

    // Ghi rồi đọc lại: %.17g không mất chính xác.
    Matrix gA, gB;
    generate_matmul_input(7, 123, gA, gB);
    write_matrix_result(tmp("a.txt"), gA);
    write_matrix_result(tmp("b.txt"), gB);
    const std::string ta = get(tmp("a.txt")), tb = get(tmp("b.txt"));
    put(in, "matmul 7\n" + ta.substr(ta.find('\n') + 1) + tb.substr(tb.find('\n') + 1));
    Matrix rA, rB;
    read_matmul_input(in, rA, rB);
    check(same(gA, rA) && same(gB, rB), "ghi roi doc lai khop tung bit");

    write_dot_result(tmp("d.txt"), 0.1 + 0.2);
    check(get(tmp("d.txt")) == "dot_result 0.30000000000000004\n", "write_dot_result dung dinh dang %.17g");
    Matrix C(2, 2);
    C(0, 0) = 19; C(0, 1) = 22; C(1, 0) = 43; C(1, 1) = 50;
    write_matrix_result(tmp("c.txt"), C);
    check(get(tmp("c.txt")) == "matrix 2\n19 22\n43 50\n", "write_matrix_result dung dinh dang");

    // load_*: chọn nguồn dữ liệu và pha thời gian.
    Options seeded;
    seeded.n = 5;
    seeded.seed = 9;
    pc::PhaseTimer t1;
    Matrix lx, ly, ex, ey;
    const long long n1 = load_dot_input(seeded, lx, ly, t1);
    generate_dot_input(5, 9, ex, ey);
    check(n1 == 5 && same(lx, ex) && same(ly, ey) && t1.seconds(pc::Phase::Init) > 0 && t1.seconds(pc::Phase::IO) == 0,
          "load khong co --input: sinh theo seed, tinh vao t_init");

    put(in, "matmul 2\n1 2\n3 4\n5 6\n7 8\n");
    Options from_file;
    from_file.input = in;
    pc::PhaseTimer t2;
    const long long n2 = load_matmul_input(from_file, A, B, t2);
    check(n2 == 2 && B(1, 1) == 8 && t2.seconds(pc::Phase::IO) > 0 && t2.seconds(pc::Phase::Init) == 0,
          "load co --input: doc file, tinh vao t_io");
    from_file.n = 3;
    threw = false;
    try { load_matmul_input(from_file, A, B, t2); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "--n khac n trong file bao loi");
}

// ---------------------------------------------------------------- csv_log

void test_csv_log() {
    std::printf("-- csv_log\n");
    CsvRecord r;
    r.problem = "dot";
    r.variant = "omp";
    r.n = 1000;
    r.block = 64;
    r.threads = 6;
    r.sched = "static";
    r.pinned = true;
    r.run = 3;
    r.seed = 42;
    r.t_init = 0.00125;
    r.t_compute = 1.5e-6;
    r.t_io = 0;
    r.t_total = 0.0013;
    r.check = 0.1 + 0.2;
    check(format_csv_row(r) == "dot,omp,1000,64,6,static,1,3,42,0.00125,1.5e-06,0,0.0013,0.30000000000000004",
          "dinh dang dong CSV dung thu tu cot");

    const std::string path = tmp("m.csv");
    append_csv_row(path, r);
    r.run = 4;
    append_csv_row(path, r);
    const std::string text = get(path);
    int lines = 0;
    for (char ch : text) lines += ch == '\n';
    check(lines == 3 && text.compare(0, std::strlen(csv_header()), csv_header()) == 0 &&
              text.find(csv_header(), 1) == std::string::npos,
          "header chi ghi mot lan, moi lan chay them mot dong");

    r.variant = "a,b";
    bool threw = false;
    try { append_csv_row(path, r); } catch (const std::invalid_argument&) { threw = true; }
    check(threw && get(path) == text, "truong chua dau phay bao loi, khong ghi gi");

    r.variant = "omp";
    put(tmp("other.csv"), "a,b,c\n1,2,3\n");
    threw = false;
    try { append_csv_row(tmp("other.csv"), r); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "file da co header khac bao loi");

    Options o;
    o.n = 8; o.seed = 7; o.b = 32; o.threads = 4; o.schedule = "dynamic"; o.pin = true; o.run = 2; o.variant = "ikj";
    pc::PhaseTimer timer;
    {
        auto scope = timer.measure(pc::Phase::Compute);
        volatile double z = 0;
        for (int i = 0; i < 100000; ++i) z = z + 1;
    }
    const CsvRecord m = make_csv_record("matmul", o, 8, timer, 12.5);
    check(m.problem == "matmul" && m.variant == "ikj" && m.n == 8 && m.block == 32 && m.threads == 4 &&
              m.sched == "dynamic" && m.pinned && m.run == 2 && m.seed == 7 && m.check == 12.5,
          "make_csv_record lay dung tham so");
    check(m.t_compute > 0 && m.t_total >= m.t_compute && m.t_init == 0, "make_csv_record lay dung thoi gian theo pha");
}

}  // namespace

int main() {
    std::error_code ec;
    g_dir = fs::temp_directory_path(ec) / ("pc_test_common_" + std::to_string(std::random_device{}()));
    if (ec || !fs::create_directories(g_dir, ec)) {
        std::printf("test_common: khong tao duoc thu muc tam\n");
        return 1;
    }

    try {
        test_cli();
        test_matrix();
        test_data_io();
        test_csv_log();
    } catch (const std::exception& e) {
        check(false, std::string("ngoai le khong mong doi: ") + e.what());
    }
    fs::remove_all(g_dir, ec);

    if (g_failures == 0) {
        std::printf("test_common: tat ca kiem tra dat\n");
        return 0;
    }
    std::printf("test_common: %d kiem tra that bai\n", g_failures);
    return 1;
}
