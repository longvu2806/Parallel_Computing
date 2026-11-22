"""Sinh dữ liệu mẫu nhỏ có đáp án biết trước vào data/sample/.

    python scripts/make_samples.py

- Đầu vào:  data/sample/<tên>.txt           (định dạng chung: "dot <n>" / "matmul <n>")
- Đáp án:   data/sample/expected/<tên>.txt  (định dạng kết quả: "dot_result <v>" / "matrix <n>")

Mọi mẫu chỉ chứa số nguyên nhỏ, nên mọi tích và tổng trong double đều chính xác: kết quả của
mọi bản (tuần tự, block, song song, mọi thứ tự cộng) phải khớp đáp án tuyệt đối, và trùng từng
byte với file --output (%.17g in số nguyên như số nguyên).
Đáp án tính bằng số nguyên Python, độc lập với mã C++ của nhóm.
Seed cố định: chạy lại cho ra đúng các file cũ. Chỉ dùng thư viện chuẩn.
"""

from __future__ import annotations

import random
from pathlib import Path

SAMPLE_DIR = Path(__file__).resolve().parent.parent / "data" / "sample"
EXPECTED_DIR = SAMPLE_DIR / "expected"


def small_ints(rng: random.Random, count: int, low: int = -4, high: int = 4) -> list[int]:
    return [rng.randint(low, high) for _ in range(count)]


def square(values: list[int], n: int) -> list[list[int]]:
    return [values[i * n:(i + 1) * n] for i in range(n)]


def line(values: list[int]) -> str:
    return " ".join(str(v) for v in values)


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n") as f:
        f.write(text)


def dot_sample(name: str, x: list[int], y: list[int]) -> None:
    assert len(x) == len(y) > 0
    write(SAMPLE_DIR / f"{name}.txt", f"dot {len(x)}\n{line(x)}\n{line(y)}\n")
    write(EXPECTED_DIR / f"{name}.txt", f"dot_result {sum(a * b for a, b in zip(x, y))}\n")


def matmul_sample(name: str, A: list[list[int]], B: list[list[int]]) -> None:
    n = len(A)
    assert n > 0 and all(len(r) == n for r in A) and len(B) == n and all(len(r) == n for r in B)
    rows = [line(r) for r in A] + [line(r) for r in B]
    write(SAMPLE_DIR / f"{name}.txt", f"matmul {n}\n" + "\n".join(rows) + "\n")
    C = [[sum(A[i][k] * B[k][j] for k in range(n)) for j in range(n)] for i in range(n)]
    write(EXPECTED_DIR / f"{name}.txt", f"matrix {n}\n" + "\n".join(line(r) for r in C) + "\n")


def main() -> None:
    # Tích vô hướng
    dot_sample("dot_4", [1, 2, 3, 4], [5, 6, 7, 8])                # ví dụ trong README: 70
    dot_sample("dot_1", [3], [-2])                                  # biên n = 1
    rng = random.Random(1000)
    dot_sample("dot_1000", small_ints(rng, 1000), small_ints(rng, 1000))   # không chia hết độ rộng vector

    # Nhân ma trận
    matmul_sample("matmul_2", [[1, 2], [3, 4]], [[5, 6], [7, 8]])  # ví dụ trong README: 19 22 / 43 50
    matmul_sample("matmul_1", [[7]], [[-3]])                        # biên n = 1
    rng = random.Random(6)
    identity = [[1 if i == j else 0 for j in range(6)] for i in range(6)]
    matmul_sample("matmul_identity_6", square(small_ints(rng, 36, -9, 9), 6), identity)  # C = A
    rng = random.Random(17)
    matmul_sample("matmul_17", square(small_ints(rng, 289), 17), square(small_ints(rng, 289), 17))  # n lẻ
    rng = random.Random(64)
    matmul_sample("matmul_64", square(small_ints(rng, 4096), 64), square(small_ints(rng, 4096), 64))

    files = sorted(SAMPLE_DIR.glob("*.txt"))
    total = sum(p.stat().st_size for p in files) + sum(p.stat().st_size for p in EXPECTED_DIR.glob("*.txt"))
    print(f"Da sinh {len(files)} mau trong {SAMPLE_DIR} ({total / 1024:.1f} KiB ke ca dap an)")


if __name__ == "__main__":
    main()
