#!/usr/bin/env python3
"""Doc ket qua do trong results/raw/, tinh thong ke cho tung cau hinh.

Chay trong PowerShell (can numpy, pandas), o thu muc goc repo:
    python scripts/plot_results.py                      # moi thi nghiem trong results/raw/ (tru smoke)
    python scripts/plot_results.py results/raw/e2_loop  # chi mot thi nghiem (thu muc hoac file .csv)

Dau vao: results/raw/<thi nghiem>/<chuong trinh>[.<build>].csv do scripts/run_experiments.sh ghi.
Ten chuong trinh lay tu ten file, vi CSV chung khong co cot nay.

Ket qua:
    - in bang tom tat ra man hinh;
    - ghi results/summary/<thi nghiem>.csv, moi dong mot cau hinh.

Thong ke (tai lieu ly thuyet, muc 2.6), tinh tren cac lan chay cua cung mot cau hinh:
    runs                  so lan chay (de bai yeu cau >= 5; it hon thi canh bao)
    median, min, max      cua t_compute va t_total (giay)
    q1, q3, iqr           phan vi 25%, 75% va IQR = q3 - q1, noi suy tuyen tinh
                          (mac dinh cua numpy.percentile; voi 5 mau, q1/q3 phu thuoc cach noi suy)
    std                   do lech chuan mau (chia m - 1)
    gflops                toc do theo median t_compute: dot 2n flop, matmul 2n^3 flop
    check                 "ok": moi lan chay cung cau hinh cho cung check (cung --seed);
                          "~":  lech o vai chu so cuoi, trong muc lam tron. Binh thuong voi ban song
                                song so thuc: OpenMP cong cac tong rieng theo thu tu thread xong truoc;
                          "LECH": lech qua 1e-9 tuong doi, can xem lai (loi hoac race condition).

"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import pandas as pd

REPO_ROOT = Path(__file__).resolve().parent.parent
RAW_DIR = REPO_ROOT / "results" / "raw"
SUMMARY_DIR = REPO_ROOT / "results" / "summary"

COLUMNS = ["problem", "variant", "n", "block", "threads", "sched", "pinned", "run", "seed",
           "t_init", "t_compute", "t_io", "t_total", "check"]
CONFIG_KEYS = ["program", "problem", "variant", "n", "block", "threads", "sched", "pinned", "seed"]
MIN_RUNS = 5
CHECK_REL_TOL = 1e-9


def load_csv(path: Path) -> pd.DataFrame:
    """Doc mot file CSV ket qua; kiem tra dung header cua hop dong trong README."""
    df = pd.read_csv(path)
    if list(df.columns) != COLUMNS:
        raise ValueError(f"{path}: header khac dinh dang chung:\n  {list(df.columns)}")
    # results/raw/e3_block/mm_block_seq.novec.csv -> thi nghiem e3_block, chuong trinh mm_block_seq,
    # build novec.
    program, _, build = path.stem.partition(".")
    df.insert(0, "experiment", path.parent.name)
    df.insert(1, "build", build or "default")
    df.insert(2, "program", program)
    return df


def find_csvs(targets: list[Path]) -> list[Path]:
    """Danh sach file CSV tu cac thu muc thi nghiem / file; mac dinh moi thi nghiem tru smoke."""
    if not targets:
        targets = sorted(d for d in RAW_DIR.iterdir() if d.is_dir() and d.name != "smoke")
    paths: list[Path] = []
    for t in targets:
        if t.is_dir():
            paths.extend(sorted(t.glob("*.csv")))
        elif t.suffix == ".csv" and t.is_file():
            paths.append(t)
        else:
            raise FileNotFoundError(f"khong phai thu muc thi nghiem hay file .csv: {t}")
    return paths


def flops(problem: str, n: int) -> float:
    if problem == "dot":
        return 2.0 * n
    if problem == "matmul":
        return 2.0 * float(n) ** 3
    return float("nan")


def summarize(df: pd.DataFrame) -> pd.DataFrame:
    """Mot dong cho moi cau hinh (thi nghiem, build, cac cot cau hinh)."""
    keys = ["experiment", "build"] + CONFIG_KEYS
    rows = []
    for key, g in df.groupby(keys, sort=True, dropna=False):
        row = dict(zip(keys, key))
        row["runs"] = len(g)
        for col in ("t_compute", "t_total"):
            t = g[col].to_numpy(dtype=float)
            q1, med, q3 = np.percentile(t, [25, 50, 75])
            row[f"{col}_median"] = med
            row[f"{col}_min"] = t.min()
            row[f"{col}_max"] = t.max()
            row[f"{col}_q1"] = q1
            row[f"{col}_q3"] = q3
            row[f"{col}_iqr"] = q3 - q1
            row[f"{col}_std"] = t.std(ddof=1) if len(t) > 1 else float("nan")
        row["t_init_median"] = float(np.median(g["t_init"]))
        row["t_io_median"] = float(np.median(g["t_io"]))
        row["gflops"] = flops(row["problem"], int(row["n"])) / row["t_compute_median"] / 1e9
        c = g["check"].to_numpy(dtype=float)
        spread = float(c.max() - c.min())
        row["check"] = float(np.median(c))
        row["check_spread"] = spread
        if spread == 0.0:
            row["check_status"] = "ok"
        elif spread <= CHECK_REL_TOL * max(1.0, abs(row["check"])):
            row["check_status"] = "~"
        else:
            row["check_status"] = "LECH"
        rows.append(row)
    return pd.DataFrame(rows)


def print_summary(s: pd.DataFrame) -> None:
    for (experiment, build), g in s.groupby(["experiment", "build"], sort=True):
        title = experiment if build == "default" else f"{experiment} (build {build})"
        print(f"\n== {title}: {len(g)} cau hinh")
        view = pd.DataFrame({
            "program": g["program"],
            "variant": g["variant"],
            "n": g["n"],
            "b": g["block"],
            "thr": g["threads"],
            "sched": g["sched"],
            "pin": g["pinned"],
            "runs": g["runs"],
            "median(s)": g["t_compute_median"].map(lambda v: f"{v:.6g}"),
            "min": g["t_compute_min"].map(lambda v: f"{v:.6g}"),
            "max": g["t_compute_max"].map(lambda v: f"{v:.6g}"),
            "IQR": g["t_compute_iqr"].map(lambda v: f"{v:.3g}"),
            "IQR/med": (g["t_compute_iqr"] / g["t_compute_median"]).map(lambda v: f"{100 * v:.1f}%"),
            "GFLOP/s": g["gflops"].map(lambda v: f"{v:.3g}"),
            "check": g["check_status"],
        })
        print(view.to_string(index=False))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Thong ke ket qua do (median, IQR, ...) cho tung cau hinh.")
    ap.add_argument("target", nargs="*", type=Path,
                    help="thu muc thi nghiem (results/raw/<ten>) hoac file .csv; bo trong = moi thi nghiem tru smoke")
    ap.add_argument("--out", type=Path, default=SUMMARY_DIR, help="thu muc ghi bang tom tat")
    args = ap.parse_args(argv)

    paths = find_csvs(args.target)
    if not paths:
        print(f"Khong co file CSV nao (tim trong {RAW_DIR})", file=sys.stderr)
        return 1

    df = pd.concat([load_csv(p) for p in paths], ignore_index=True)
    s = summarize(df)
    print_summary(s)

    args.out.mkdir(parents=True, exist_ok=True)
    for (experiment, build), g in s.groupby(["experiment", "build"], sort=True):
        name = experiment if build == "default" else f"{experiment}.{build}"
        out = args.out / f"{name}.csv"
        g.to_csv(out, index=False, float_format="%.9g")
        print(f"\nDa ghi {out.relative_to(REPO_ROOT) if out.is_relative_to(REPO_ROOT) else out}")

    few = s[s["runs"] < MIN_RUNS]
    bad = s[s["check_status"] == "LECH"]
    if not few.empty:
        print(f"\nCANH BAO: {len(few)} cau hinh co it hon {MIN_RUNS} lan chay", file=sys.stderr)
    if not bad.empty:
        print(f"CANH BAO: {len(bad)} cau hinh co check khac nhau giua cac lan chay", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
