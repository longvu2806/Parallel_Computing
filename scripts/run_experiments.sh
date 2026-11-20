#!/usr/bin/env bash
# Chay thi nghiem theo file cau hinh: moi cau hinh chay N lan (mac dinh 5), thu tu cac lan chay
# duoc xao tron, moi lan chay ghi mot dong CSV vao results/raw/<ten thi nghiem>.csv.
#
# Chay trong MSYS2 UCRT64, o thu muc goc repo:
#   scripts/run_experiments.sh --check                    kiem tra toolchain tren may do
#   scripts/run_experiments.sh scripts/experiments/e1_dot.conf
#   scripts/run_experiments.sh -r 5 --dry-run scripts/experiments/*.conf
#
# Tuy chon:
#   -r N         so lan chay moi cau hinh (mac dinh 5; de bai yeu cau toi thieu 5)
#   -o DIR       thu muc ket qua (mac dinh results/raw)
#   -s SEED      seed xao tron thu tu chay (mac dinh 2026), ghi vao file .meta.txt
#   -p GIAY      nghi giua hai lan chay, giam nong may (mac dinh 0)
#   -t GIAY      gioi han thoi gian moi lan chay, 0 = khong gioi han (mac dinh 0)
#   --no-verify  bo qua luot kiem tra --verify truoc khi do
#   --fresh      xoa ket qua cu cua thi nghiem (.csv, .done, .log, .meta.txt) roi chay lai tu dau
#   --dry-run    chi in cac lenh se chay, khong chay
#   --check      kiem tra toolchain (compiler, OpenMP, so thread, make test) roi thoat
#   -h, --help   in huong dan nay
#
# Dinh dang file cau hinh (.conf), moi dong mot cau hinh:
#   # chu thich
#   dot_seq --variant {plain,simd} --n {8192,262144}
#   dot_omp --n 100000000 --threads {1,2,4,6,12}
#   @build novec -std=c++17 -O3 -fno-tree-vectorize -Wall -Wextra -fopenmp
#   mm_block_seq --n 1024 --b {32,64}
#   @build default
# - Tu dau tien la ten chuong trinh trong build/; phan con lai la tham so, KHONG ghi
#   --run, --csv (script tu them).
# - {a,b,c} duoc khai trien thanh tich De-cac cua moi cap ngoac tren dong.
# - "@build TEN CXXFLAGS..." build ban rieng vao build/TEN voi co bien dich do; cac dong sau
#   dung ban nay va ghi vao <thi nghiem>.TEN.csv (CSV khong co cot co bien dich, nen tach file).
#   "@build default" quay lai build/ mac dinh.
#
# Quy trinh:
#   1. make (va make cho tung @build).
#   2. Luot kiem tra: chay moi cau hinh mot lan voi --verify, khong ghi CSV. Co cau hinh sai
#      thi dung, khong do. Luot nay cung la luot khoi dong (nap chuong trinh, cache dia).
#   3. Lap danh sach (cau hinh, lan chay), bo cac lan da xong (file .done), xao tron, chay.
#      Moi lan chay xong thi ghi vao .done, nen bi ngat giua chung chi can chay lai dung lenh cu.
#      Loi cua tung lan chay ghi vao .log, script chay tiep va tra ma 1 o cuoi.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

REPS=5
OUT_DIR="results/raw"
SHUFFLE_SEED=2026
PAUSE=0
TIME_LIMIT=0
VERIFY=1
FRESH=0
DRY_RUN=0
CHECK=0
CONFIGS=()

die() { echo "Loi: $*" >&2; exit 2; }

usage() { sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        -r) REPS="${2:?thieu gia tri cho -r}"; shift 2 ;;
        -o) OUT_DIR="${2:?thieu gia tri cho -o}"; shift 2 ;;
        -s) SHUFFLE_SEED="${2:?thieu gia tri cho -s}"; shift 2 ;;
        -p) PAUSE="${2:?thieu gia tri cho -p}"; shift 2 ;;
        -t) TIME_LIMIT="${2:?thieu gia tri cho -t}"; shift 2 ;;
        --no-verify) VERIFY=0; shift ;;
        --fresh) FRESH=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        --check) CHECK=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) die "tuy chon khong ho tro: $1 (xem --help)" ;;
        *) CONFIGS+=("$1"); shift ;;
    esac
done

[[ "$REPS" =~ ^[1-9][0-9]*$ ]] || die "-r phai la so nguyen duong: $REPS"
[[ "$PAUSE" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "-p phai la so giay: $PAUSE"
[[ "$TIME_LIMIT" =~ ^[0-9]+$ ]] || die "-t phai la so giay nguyen: $TIME_LIMIT"

# ------------------------------------------------------------------ thong tin moi truong

# Commit hien tai. Shell MSYS2 khong thay git cua Windows, nen thu git roi doc thang .git/.
git_commit() {
    local g
    for g in git "/c/Program Files/Git/cmd/git.exe"; do
        if command -v "$g" >/dev/null 2>&1; then
            local c
            if c="$("$g" -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null)"; then
                local dirty=""
                [[ -n "$("$g" -C "$REPO_ROOT" status --porcelain --untracked-files=no 2>/dev/null)" ]] && dirty=" (co thay doi chua commit)"
                echo "$c$dirty"
                return
            fi
        fi
    done
    local head
    head="$(cat .git/HEAD 2>/dev/null || true)"
    if [[ "$head" == ref:* ]]; then
        local ref="${head#ref: }"
        if [[ -f ".git/$ref" ]]; then cut -c1-7 ".git/$ref"; return; fi
        local packed
        packed="$(grep " $ref\$" .git/packed-refs 2>/dev/null || true)"
        if [[ -n "$packed" ]]; then echo "${packed:0:7}"; return; fi
        echo "?"
        return
    fi
    echo "${head:0:7}"
}

cpu_name() { grep -m1 'model name' /proc/cpuinfo 2>/dev/null | sed 's/.*: //' || echo "?"; }
ram_total() { awk '/MemTotal/ {printf "%.1f GB", $2 / 1048576}' /proc/meminfo 2>/dev/null || echo "?"; }
default_cxxflags() { sed -n 's/^CXXFLAGS[[:space:]]*?=[[:space:]]*//p' Makefile; }

# ------------------------------------------------------------------ --check

run_check() {
    echo "== Kiem tra toolchain tren may do ($(date '+%Y-%m-%d %H:%M:%S'))"
    echo "He dieu hanh : $(uname -s -r -m)"
    echo "MSYSTEM      : ${MSYSTEM:-?}"
    echo "CPU          : $(cpu_name)"
    echo "CPU logic    : $(nproc)"
    echo "RAM          : $(ram_total)"
    echo "Commit       : $(git_commit)"
    echo "Co mac dinh  : $(default_cxxflags)"

    local gxx
    gxx="$(command -v g++ || true)"
    [[ -n "$gxx" ]] || die "khong tim thay g++; mo MSYS2 UCRT64 va cai mingw-w64-ucrt-x86_64-gcc"
    echo "g++          : $gxx"
    echo "Phien ban    : $(g++ --version | head -1)"
    echo "Target       : $(g++ -dumpmachine)"
    local ok=1
    if [[ "$gxx" != /ucrt64/* ]]; then
        echo "CANH BAO: g++ khong phai ban MSYS2 UCRT64 (/ucrt64/bin/g++). Hay chay trong shell UCRT64."
        ok=0
    fi
    echo "make         : $(make --version | head -1)"

    # OpenMP: phien ban, so thread mac dinh, doi thread thuc te, do phan giai omp_get_wtime.
    local tmp
    tmp="$(mktemp -d)"
    cat > "$tmp/omp_check.cpp" <<'EOF'
#include <omp.h>
#include <chrono>
#include <cstdio>
int main() {
    int team = 0;
#pragma omp parallel
    {
#pragma omp single
        team = omp_get_num_threads();
    }
    // Buoc nho nhat cua steady_clock
    double best = 1.0;
    for (int r = 0; r < 1000; ++r) {
        auto t0 = std::chrono::steady_clock::now(), t1 = t0;
        while (t1 == t0) t1 = std::chrono::steady_clock::now();
        double d = std::chrono::duration<double>(t1 - t0).count();
        if (d < best) best = d;
    }
    std::printf("_OPENMP      : %d\n", _OPENMP);
    std::printf("omp_get_num_procs = %d, omp_get_max_threads = %d, doi thread thuc te = %d\n",
                omp_get_num_procs(), omp_get_max_threads(), team);
    std::printf("omp_get_wtick = %g s, buoc steady_clock = %.0f ns\n", omp_get_wtick(), best * 1e9);
    return team == omp_get_num_procs() ? 0 : 1;
}
EOF
    if g++ -std=c++17 -O2 -fopenmp "$tmp/omp_check.cpp" -o "$tmp/omp_check" && "$tmp/omp_check"; then
        echo "OpenMP       : OK, chay du $(nproc) thread"
    else
        echo "OpenMP       : LOI (khong bien dich duoc hoac khong chay du thread)"
        ok=0
    fi
    rm -rf "$tmp"
    for v in OMP_NUM_THREADS OMP_PROC_BIND OMP_PLACES OMP_WAIT_POLICY OMP_DYNAMIC GOMP_SPINCOUNT; do
        if [[ -n "${!v:-}" ]]; then echo "CANH BAO: bien moi truong $v=${!v} dang dat, co the anh huong so do"; fi
    done

    echo "== make && make test"
    if make -s && make -s test; then
        echo "make test     : OK"
    else
        echo "make test     : LOI"
        ok=0
    fi
    if [[ $ok -eq 1 ]]; then
        echo "== Toolchain san sang"
        return 0
    fi
    echo "== Toolchain CO VAN DE, xem cac dong o tren"
    return 1
}

if [[ $CHECK -eq 1 ]]; then
    run_check
    exit $?
fi

[[ ${#CONFIGS[@]} -gt 0 ]] || { usage; die "can it nhat mot file cau hinh"; }

# ------------------------------------------------------------------ doc file cau hinh

# Khai trien {a,b,c}: in moi to hop tren mot dong.
expand_braces() {
    local line="$1"
    if [[ "$line" =~ ^([^{]*)\{([^}]*)\}(.*)$ ]]; then
        local pre="${BASH_REMATCH[1]}" alts="${BASH_REMATCH[2]}" post="${BASH_REMATCH[3]}"
        local -a parts
        IFS=',' read -r -a parts <<< "$alts"
        local p
        for p in "${parts[@]}"; do
            expand_braces "$pre$p$post"
        done
    else
        echo "$line"
    fi
}

# Moi cau hinh: "<thu muc build>|<file csv>|<chuong trinh va tham so>"
ENTRIES=()
declare -A BUILDS=()      # ten build -> co bien dich
declare -A EXPERIMENTS=() # ten thi nghiem -> 1

parse_config() {
    local conf="$1"
    [[ -f "$conf" ]] || die "khong co file cau hinh: $conf"
    local exp
    exp="$(basename "$conf" .conf)"
    [[ "$exp" =~ ^[A-Za-z0-9_-]+$ ]] || die "ten file cau hinh chi duoc gom chu, so, _ va -: $conf"
    EXPERIMENTS[$exp]=1
    local build_dir="build" csv="$OUT_DIR/$exp.csv"
    local lineno=0 raw line
    while IFS= read -r raw || [[ -n "$raw" ]]; do
        lineno=$((lineno + 1))
        line="${raw%$'\r'}"                       # file soan tren Windows co the co CRLF
        line="${line%%#*}"                        # bo chu thich
        line="$(echo "$line" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
        [[ -z "$line" ]] && continue
        if [[ "$line" == @build* ]]; then
            local name flags
            read -r _ name flags <<< "$line"
            [[ -n "$name" ]] || die "$conf:$lineno: @build can ten"
            if [[ "$name" == default ]]; then
                build_dir="build"; csv="$OUT_DIR/$exp.csv"
                continue
            fi
            [[ "$name" =~ ^[A-Za-z0-9_-]+$ ]] || die "$conf:$lineno: ten build khong hop le: $name"
            [[ -n "$flags" ]] || die "$conf:$lineno: @build $name can co bien dich"
            if [[ -n "${BUILDS[$name]:-}" && "${BUILDS[$name]}" != "$flags" ]]; then
                die "$conf:$lineno: build $name da khai bao voi co khac"
            fi
            BUILDS[$name]="$flags"
            build_dir="build/$name"; csv="$OUT_DIR/$exp.$name.csv"
            continue
        fi
        if [[ "$line" =~ (^|[[:space:]])--(run|csv)([[:space:]]|$) ]]; then
            die "$conf:$lineno: khong ghi --run, --csv trong cau hinh (script tu them)"
        fi
        local e
        while IFS= read -r e; do
            ENTRIES+=("$build_dir|$csv|$e")
        done < <(expand_braces "$line")
    done < "$conf"
}

for conf in "${CONFIGS[@]}"; do
    parse_config "$conf"
done
[[ ${#ENTRIES[@]} -gt 0 ]] || die "file cau hinh khong co cau hinh nao"

# ------------------------------------------------------------------ build

if [[ $DRY_RUN -eq 0 ]]; then
    echo "== make (build/, co: ${CXXFLAGS:-$(default_cxxflags)})"
    make -s
    for name in "${!BUILDS[@]}"; do
        echo "== make build/$name (co: ${BUILDS[$name]})"
        make -s BUILD_DIR="build/$name" CXXFLAGS="${BUILDS[$name]}"
    done
fi

# Duong dan chuong trinh; g++ tren Windows tao file .exe.
program_path() {
    local dir="$1" prog="$2"
    if [[ -x "$dir/$prog" ]]; then echo "$dir/$prog"
    elif [[ -x "$dir/$prog.exe" ]]; then echo "$dir/$prog.exe"
    else return 1
    fi
}

if [[ $DRY_RUN -eq 0 ]]; then
    for entry in "${ENTRIES[@]}"; do
        IFS='|' read -r dir _ cmd <<< "$entry"
        prog="${cmd%% *}"
        program_path "$dir" "$prog" >/dev/null || die "khong co chuong trinh $dir/$prog (sai ten trong cau hinh?)"
    done
fi

# Chay mot lenh, co gioi han thoi gian neu dat -t.
run_limited() {
    if [[ "$TIME_LIMIT" -gt 0 ]]; then timeout "$TIME_LIMIT" "$@"; else "$@"; fi
}

# ------------------------------------------------------------------ dry run

if [[ $DRY_RUN -eq 1 ]]; then
    echo "== ${#ENTRIES[@]} cau hinh x $REPS lan = $(( ${#ENTRIES[@]} * REPS )) lan chay"
    for entry in "${ENTRIES[@]}"; do
        IFS='|' read -r dir csv cmd <<< "$entry"
        prog="${cmd%% *}"; args="${cmd#"$prog"}"
        echo "$dir/$prog$args --run <0..$((REPS - 1))> --csv $csv"
    done
    exit 0
fi

mkdir -p "$OUT_DIR"

if [[ $FRESH -eq 1 ]]; then
    for exp in "${!EXPERIMENTS[@]}"; do
        echo "== --fresh: xoa ket qua cu cua $exp"
        rm -f "$OUT_DIR/$exp".csv "$OUT_DIR/$exp".*.csv "$OUT_DIR/$exp".done "$OUT_DIR/$exp".log \
              "$OUT_DIR/$exp".meta.txt
    done
fi

# ------------------------------------------------------------------ luot kiem tra --verify

if [[ $VERIFY -eq 1 ]]; then
    echo "== Luot kiem tra: ${#ENTRIES[@]} cau hinh, moi cau hinh 1 lan voi --verify (khong ghi CSV)"
    vfail=0
    for entry in "${ENTRIES[@]}"; do
        IFS='|' read -r dir _ cmd <<< "$entry"
        read -r -a words <<< "$cmd"
        bin="$(program_path "$dir" "${words[0]}")"
        if ! out="$(run_limited "$bin" "${words[@]:1}" --verify 2>&1 >/dev/null)"; then
            echo "  SAI: $dir/$cmd --verify"
            if [[ -n "$out" ]]; then echo "       $out"; fi
            vfail=$((vfail + 1))
        fi
    done
    if [[ $vfail -gt 0 ]]; then
        die "$vfail cau hinh khong qua --verify; sua loi truoc khi do (hoac --no-verify neu biet ro ly do)"
    fi
    echo "   tat ca dat"
fi

# ------------------------------------------------------------------ danh sach lan chay

# Ghi thong tin phien chay vao <thi nghiem>.meta.txt (moi phien them mot khoi).
for exp in "${!EXPERIMENTS[@]}"; do
    {
        echo "== Phien chay $(date '+%Y-%m-%d %H:%M:%S')"
        echo "cau hinh   : ${CONFIGS[*]}"
        echo "commit     : $(git_commit)"
        echo "cpu        : $(cpu_name), $(nproc) CPU logic"
        echo "ram        : $(ram_total)"
        echo "he dieu hanh: $(uname -s -r -m)"
        echo "compiler   : $(g++ --version | head -1)"
        echo "co build/  : ${CXXFLAGS:-$(default_cxxflags)}"
        for name in "${!BUILDS[@]}"; do echo "co build/$name: ${BUILDS[$name]}"; done
        echo "so lan     : $REPS, seed xao tron: $SHUFFLE_SEED, nghi: ${PAUSE}s, gioi han: ${TIME_LIMIT}s"
        echo "bien OMP   : $(env | grep -E '^(OMP_|GOMP_)' | tr '\n' ' ')"
    } >> "$OUT_DIR/$exp.meta.txt"
done

exp_of_csv() { local b; b="$(basename "$1" .csv)"; echo "${b%%.*}"; }

# Cac lan chay da xong tu phien truoc (khoa: "<build>|<lenh>|<lan>").
declare -A DONE=()
for exp in "${!EXPERIMENTS[@]}"; do
    if [[ -f "$OUT_DIR/$exp.done" ]]; then
        while IFS= read -r key; do DONE["$key"]=1; done < "$OUT_DIR/$exp.done"
    fi
done

JOBS=()
skipped=0
for i in "${!ENTRIES[@]}"; do
    IFS='|' read -r dir _ cmd <<< "${ENTRIES[$i]}"
    for ((r = 0; r < REPS; r++)); do
        if [[ -n "${DONE["$dir|$cmd|$r"]:-}" ]]; then skipped=$((skipped + 1)); continue; fi
        JOBS+=("$i|$r")
    done
done

total=${#JOBS[@]}
echo "== ${#ENTRIES[@]} cau hinh x $REPS lan: $total lan can chay, $skipped lan da xong tu truoc"
[[ $total -eq 0 ]] && { echo "== Khong con gi de chay"; exit 0; }

mapfile -t JOBS < <(printf '%s\n' "${JOBS[@]}" | shuf --random-source=<(yes "$SHUFFLE_SEED"))

# ------------------------------------------------------------------ chay

failed=0
start=$(date +%s)
k=0
for job in "${JOBS[@]}"; do
    k=$((k + 1))
    i="${job%%|*}"; r="${job##*|}"
    IFS='|' read -r dir csv cmd <<< "${ENTRIES[$i]}"
    read -r -a words <<< "$cmd"
    bin="$(program_path "$dir" "${words[0]}")"
    exp="$(exp_of_csv "$csv")"

    if err="$(run_limited "$bin" "${words[@]:1}" --run "$r" --csv "$csv" 2>&1 >/dev/null)"; then
        echo "$dir|$cmd|$r" >> "$OUT_DIR/$exp.done"
        status="ok"
    else
        code=$?
        failed=$((failed + 1))
        status="LOI (ma $code)"
        {
            echo "$(date '+%Y-%m-%d %H:%M:%S') ma $code: $dir/$cmd --run $r --csv $csv"
            if [[ -n "$err" ]]; then echo "    $err"; fi
        } >> "$OUT_DIR/$exp.log"
    fi

    now=$(date +%s)
    elapsed=$((now - start))
    eta=$(( elapsed * (total - k) / k ))
    printf '[%d/%d] %-60s run %d  %s  (da chay %ds, con khoang %ds)\n' \
        "$k" "$total" "$dir/$cmd" "$r" "$status" "$elapsed" "$eta"

    if [[ "$PAUSE" != 0 && $k -lt $total ]]; then sleep "$PAUSE"; fi
done

echo "== Xong: $((total - failed))/$total lan chay thanh cong trong $(( $(date +%s) - start ))s"
for exp in "${!EXPERIMENTS[@]}"; do
    ls "$OUT_DIR/$exp".csv "$OUT_DIR/$exp".*.csv 2>/dev/null | sed 's/^/   ket qua: /' || true
done
if [[ $failed -gt 0 ]]; then
    echo "== $failed lan chay loi, xem $OUT_DIR/<thi nghiem>.log; chay lai dung lenh nay de chay bu" >&2
    exit 1
fi
