#pragma once

// Bộ đọc tham số dòng lệnh dùng chung cho mọi chương trình trong dot_product/ và matmul/.
// Danh sách tham số và giá trị mặc định khớp với bảng "Tham số dòng lệnh" trong README.
//
// Cách dùng trong main():
//     Options opt = parse_options_or_exit(argc, argv);
//
// parse_options() ném std::invalid_argument khi tham số sai, để test kiểm tra được;
// parse_options_or_exit() in lỗi ra stderr và thoát với mã 2 (theo hợp đồng trong README).

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

struct Options {
    long long n = 0;                // kích thước; 0 nghĩa là chưa đặt (khi đó phải có --input)
    unsigned long long seed = 42;   // seed sinh dữ liệu
    std::string input;              // --input: đọc dữ liệu từ file thay vì sinh theo seed
    std::string output;             // --output: ghi kết quả ra file
    std::string csv;                // --csv: ghi thêm một dòng kết quả đo
    int b = 64;                     // block size
    int threads = 1;                // số thread OpenMP
    std::string variant;            // biến thể; rỗng nghĩa là chương trình dùng mặc định của nó
    std::string schedule = "static";
    bool pin = false;               // ghim thread vào lõi vật lý
    int run = 0;                    // số thứ tự lần chạy, ghi vào CSV
    bool verify = false;            // so kết quả với bản tham chiếu
    bool help = false;              // --help
};

namespace cli_detail {

// Đọc số nguyên có dấu, bắt buộc cả chuỗi là số và nằm trong [low, high].
inline long long to_ll(const std::string& name, const std::string& text, long long low, long long high) {
    if (text.empty()) throw std::invalid_argument(name + " can mot so nguyen");
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0') throw std::invalid_argument(name + " khong phai so nguyen hop le: " + text);
    if (value < low || value > high) {
        throw std::invalid_argument(name + " phai nam trong [" + std::to_string(low) + ", " + std::to_string(high) + "]");
    }
    return value;
}

// Đọc số nguyên không dấu (seed). strtoull chấp nhận dấu '-', nên phải chặn riêng.
inline unsigned long long to_ull(const std::string& name, const std::string& text) {
    if (text.empty() || text[0] == '-' || text[0] == '+') {
        throw std::invalid_argument(name + " phai la so nguyen khong am: " + text);
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || *end != '\0') throw std::invalid_argument(name + " khong phai so nguyen hop le: " + text);
    return value;
}

}  // namespace cli_detail

inline const char* usage_text() {
    return "Tham so:\n"
           "  --n <so>              kich thuoc vector / cap ma tran (khi sinh theo seed)\n"
           "  --seed <so>           seed sinh du lieu (mac dinh 42)\n"
           "  --input <file>        doc du lieu tu file dinh dang chung (thay cho --n, --seed)\n"
           "  --output <file>       ghi ket qua ra file dinh dang chung\n"
           "  --csv <file>          ghi them mot dong ket qua do\n"
           "  --b <so>              block size (mac dinh 64)\n"
           "  --threads <so>        so thread OpenMP (mac dinh 1)\n"
           "  --variant <ten>       bien the, vi du ijk, ikj, barrier\n"
           "  --schedule <ten>      static hoac dynamic (mac dinh static)\n"
           "  --pin 0|1             ghim thread vao loi vat ly (mac dinh 0)\n"
           "  --run <so>            so thu tu lan chay, ghi vao CSV (mac dinh 0)\n"
           "  --verify              so ket qua voi ban tham chieu\n"
           "  --help                in huong dan nay\n";
}

inline Options parse_options(int argc, char** argv) {
    using cli_detail::to_ll;
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];

        // Hai cờ không có giá trị đi kèm
        if (key == "--verify") { opt.verify = true; continue; }
        if (key == "--help" || key == "-h") { opt.help = true; continue; }

        // Mọi tham số còn lại đều cần đúng một giá trị ngay sau nó
        if (i + 1 >= argc) throw std::invalid_argument("thieu gia tri cho " + key);
        const std::string value = argv[++i];

        if (key == "--n")              opt.n = to_ll(key, value, 1, 4'000'000'000LL);
        else if (key == "--seed")      opt.seed = cli_detail::to_ull(key, value);
        else if (key == "--input")     opt.input = value;
        else if (key == "--output")    opt.output = value;
        else if (key == "--csv")       opt.csv = value;
        else if (key == "--b")         opt.b = static_cast<int>(to_ll(key, value, 1, 1'000'000));
        else if (key == "--threads")   opt.threads = static_cast<int>(to_ll(key, value, 1, 1024));
        else if (key == "--variant")   opt.variant = value;
        else if (key == "--schedule") {
            if (value != "static" && value != "dynamic") {
                throw std::invalid_argument("--schedule phai la static hoac dynamic: " + value);
            }
            opt.schedule = value;
        }
        else if (key == "--pin")       opt.pin = to_ll(key, value, 0, 1) == 1;
        else if (key == "--run")       opt.run = static_cast<int>(to_ll(key, value, 0, 1'000'000));
        else throw std::invalid_argument("tham so khong ho tro: " + key);
    }

    // Phải biết lấy dữ liệu từ đâu: sinh theo seed (cần --n) hoặc đọc file (--input)
    if (!opt.help && opt.input.empty() && opt.n == 0) {
        throw std::invalid_argument("can --n (sinh theo seed) hoac --input (doc tu file)");
    }
    return opt;
}

inline Options parse_options_or_exit(int argc, char** argv) {
    try {
        Options opt = parse_options(argc, argv);
        if (opt.help) {
            std::fputs(usage_text(), stdout);
            std::exit(0);
        }
        return opt;
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "Loi tham so: %s\n%s", e.what(), usage_text());
        std::exit(2);
    }
}
