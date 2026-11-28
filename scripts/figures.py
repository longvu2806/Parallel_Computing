"""Speedup, efficiency, Karp-Flatt va bieu do cho plot_results.py (viec C6).

Khong chay truc tiep file nay; dung:  python scripts/plot_results.py [thi nghiem ...]

Moc T1 (ke hoach muc 6: "ban tuan tu tot nhat, co cung muc vectorization voi ban song song"):
    - chi lay trong CUNG thi nghiem (cung phien do): so lieu giua cac phien khong so sanh duoc,
      vi trang thai may (nguon, nhiet) thay doi (docs/ket_qua_tile_shape.md, muc 5);
    - trong cac chuong trinh tuan tu (*_seq) cung bai toan, cung n, cung muc vectorization
      (variant bat dau bang "novec" hoac la "plain" -> vo huong; con lai -> simd),
      lay cau hinh co median t_compute nho nhat (moi block size, moi thu tu vong lap).

Voi cau hinh chay p thread, thoi gian median Tp (tai lieu ly thuyet, muc 3):
    speedup      S = T1 / Tp
    efficiency   E = S / p
    Karp-Flatt   e = (1/S - 1/p) / (1 - 1/p)   (p > 1): ti le phan "noi tiep" do duoc; e tang theo p
                 nghia la chi phi song song (dong bo, bang thong) tang, khong chi do phan tuan tu.
    S_self       T(cung chuong trinh, cung cau hinh, 1 thread) / Tp: kha nang mo rong cua rieng
                 ban song song, tach khoi chi phi cua no o 1 thread so voi ban tuan tu.
Thanh loi cua speedup: T1 / Tp_q3 den T1 / Tp_q1 (khoang IQR cua Tp; T1 lay median).
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
import pandas as pd

# Bang mau phan loai (thu tu co dinh, da kiem dinh mu mau tren nen sang), van ban mau xam.
SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
SURFACE = "#fcfcfb"
TEXT = "#0b0b0b"
TEXT_2 = "#52514e"
GRID = "#e4e3df"
REF = "#9b9a96"          # duong tham chieu (ly tuong, so loi vat ly)
PHYSICAL_CORES = 6       # may do i7-9850H

SEQ_SUFFIX = "_seq"


def vec_class(variant: str) -> str:
    v = str(variant)
    return "scalar" if v.startswith("novec") or v == "plain" else "simd"


def is_sequential(program: str) -> bool:
    return str(program).endswith(SEQ_SUFFIX)


# ---------------------------------------------------------------- speedup

def compute_speedup(summary: pd.DataFrame) -> pd.DataFrame:
    """Them T1 va speedup cho moi cau hinh khong tuan tu. summary: ket qua cua summarize()."""
    s = summary.copy()
    s["vec"] = s["variant"].map(vec_class)
    seq = s[s["program"].map(is_sequential)]
    rows = []
    for _, r in s[~s["program"].map(is_sequential)].iterrows():
        base = seq[(seq["experiment"] == r["experiment"]) & (seq["problem"] == r["problem"]) &
                   (seq["n"] == r["n"]) & (seq["vec"] == r["vec"])]
        out = r.to_dict()
        if base.empty:
            out.update(t1=np.nan, baseline="")
        else:
            b = base.loc[base["t_compute_median"].idxmin()]
            out["t1"] = b["t_compute_median"]
            out["baseline"] = f"{b['program']} {b['variant']} b={b['block']}"
        p = int(r["threads"])
        tp = r["t_compute_median"]
        out["speedup"] = out["t1"] / tp
        out["speedup_lo"] = out["t1"] / r["t_compute_q3"]
        out["speedup_hi"] = out["t1"] / r["t_compute_q1"]
        out["efficiency"] = out["speedup"] / p
        out["karp_flatt"] = (1 / out["speedup"] - 1 / p) / (1 - 1 / p) if p > 1 else np.nan
        same = s[(s["experiment"] == r["experiment"]) & (s["build"] == r["build"]) &
                 (s["program"] == r["program"]) & (s["variant"] == r["variant"]) & (s["n"] == r["n"]) &
                 (s["block"] == r["block"]) & (s["sched"] == r["sched"]) & (s["pinned"] == r["pinned"]) &
                 (s["threads"] == 1)]
        out["speedup_self"] = same["t_compute_median"].iloc[0] / tp if not same.empty else np.nan
        rows.append(out)
    cols = ["experiment", "build", "program", "problem", "variant", "n", "block", "threads", "sched", "pinned",
            "runs", "t_compute_median", "t1", "baseline", "speedup", "speedup_lo", "speedup_hi", "efficiency",
            "karp_flatt", "speedup_self"]
    return pd.DataFrame(rows, columns=cols)


# ---------------------------------------------------------------- ve

def _style(ax, xlabel: str, ylabel: str) -> None:
    ax.set_facecolor(SURFACE)
    ax.grid(True, color=GRID, linewidth=0.8, linestyle="-")
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(GRID)
    ax.tick_params(colors=TEXT_2, labelsize=9)
    ax.set_xlabel(xlabel, color=TEXT_2, fontsize=9)
    ax.set_ylabel(ylabel, color=TEXT_2, fontsize=9)


def _new_figure(nrows: int, ncols: int, width: float, height: float):
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(nrows, ncols, figsize=(width, height), squeeze=False, facecolor=SURFACE)
    return fig, axes


def _save(fig, path: Path, title: str, subtitle: str, legend_rows: int = 0) -> Path:
    import matplotlib.pyplot as plt
    h = fig.get_figheight()  # inch; dat tieu de theo inch de khong chong nhau o moi chieu cao
    fig.text(0.01, 1 - 0.18 / h, title, ha="left", va="top", fontsize=12, color=TEXT, fontweight="bold")
    fig.text(0.01, 1 - 0.45 / h, subtitle, ha="left", va="top", fontsize=9, color=TEXT_2)
    fig.tight_layout(rect=(0, (0.1 + 0.25 * legend_rows) / h, 1, 1 - 0.7 / h))
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=150, facecolor=SURFACE)
    plt.close(fig)
    return path


def _line(ax, x, y, color, label, lo=None, hi=None):
    if lo is not None and hi is not None:
        yerr = np.vstack([np.asarray(y) - np.asarray(lo), np.asarray(hi) - np.asarray(y)])
        ax.errorbar(x, y, yerr=yerr, fmt="none", ecolor=color, elinewidth=1, capsize=2.5, alpha=0.8)
    ax.plot(x, y, color=color, linewidth=2, marker="o", markersize=6, markeredgecolor=SURFACE,
            markeredgewidth=1.5, label=label, solid_joinstyle="round", solid_capstyle="round")


def _series_label(r, with_block: bool = False) -> str:
    parts = [str(r["program"]), str(r["variant"])]
    if with_block:
        parts.append(f"b={int(r['block'])}")
    if r["build"] != "default":
        parts.append(f"build {r['build']}")
    if int(r["pinned"]):
        parts.append("ghim")
    return " ".join(parts)


def plot_scaling(sp: pd.DataFrame, fig_dir: Path) -> list[Path]:
    """Moi (thi nghiem, n): luoi 3 hang (speedup, efficiency, Karp-Flatt) x cot theo lich."""
    out = []
    if sp.empty:
        return out
    for (exp, problem, n), g in sp.groupby(["experiment", "problem", "n"]):
        if g["threads"].nunique() < 2:
            continue
        g = g.copy()
        # Cung chuong trinh chay nhieu block size (vi du E5: b = 48 va b = 32) thi tach thanh duong rieng.
        with_block = bool((g.groupby("program")["block"].nunique() > 1).any())
        g["label"] = g.apply(lambda r: _series_label(r, with_block), axis=1)
        labels = sorted(g["label"].unique())
        if len(labels) > len(SERIES):
            print(f"CANH BAO: {exp} n={n}: {len(labels)} duong, qua {len(SERIES)} mau; bo qua bieu do")
            continue
        color = {lab: SERIES[i] for i, lab in enumerate(labels)}   # mau theo doi tuong, co dinh
        scheds = sorted(g["sched"].unique())
        fig, axes = _new_figure(3, len(scheds), 4.6 * len(scheds), 9.6)
        pmax = int(g["threads"].max())
        for c, sc in enumerate(scheds):
            gs = g[g["sched"] == sc]
            ax_s, ax_e, ax_k = axes[0][c], axes[1][c], axes[2][c]
            ideal = np.arange(1, pmax + 1)
            ax_s.plot(ideal, ideal, color=REF, linewidth=1, label="lý tưởng (S = p)")
            ax_e.axhline(1.0, color=REF, linewidth=1)
            for lab in labels:
                d = gs[gs["label"] == lab].sort_values("threads")
                if d.empty:
                    continue
                x = d["threads"].to_numpy()
                _line(ax_s, x, d["speedup"], color[lab], lab, d["speedup_lo"], d["speedup_hi"])
                _line(ax_e, x, d["efficiency"], color[lab], lab, d["speedup_lo"] / x, d["speedup_hi"] / x)
                k = d[d["threads"] > 1]
                if not k.empty:
                    _line(ax_k, k["threads"].to_numpy(), k["karp_flatt"], color[lab], lab)
            for ax in (ax_s, ax_e, ax_k):
                if pmax > PHYSICAL_CORES:
                    ax.axvline(PHYSICAL_CORES, color=REF, linewidth=1)
                ticks = sorted(g["threads"].unique())
                ax.set_xticks(ticks)
            ymax = max(float(gs["speedup_hi"].max()), 1.0)
            ax_s.set_ylim(0, max(ymax * 1.15, min(pmax, ymax * 2)))
            if pmax > PHYSICAL_CORES:
                ax_s.text(PHYSICAL_CORES, ax_s.get_ylim()[1] * 0.97, f" {PHYSICAL_CORES} lõi vật lý",
                          color=TEXT_2, fontsize=8, va="top")
            ax_e.set_ylim(0, max(1.15, float(gs["efficiency"].max()) * 1.15))
            kf = gs["karp_flatt"].dropna()
            if not kf.empty:
                ax_k.set_ylim(min(0.0, float(kf.min()) * 1.15), max(0.05, float(kf.max()) * 1.15))
            ax_k.set_xlim(ax_s.get_xlim())
            _style(ax_s, "", "Speedup S = T1 / Tp")
            _style(ax_e, "", "Efficiency E = S / p")
            _style(ax_k, "Số thread p", "Karp–Flatt e")
            ax_s.set_title(f"lịch {sc}", fontsize=10, color=TEXT, loc="left")
        handles, names = axes[0][0].get_legend_handles_labels()
        ncol = min(4, len(names))
        fig.legend(handles, names, loc="lower center", ncol=ncol, frameon=False, fontsize=8,
                   labelcolor=TEXT, bbox_to_anchor=(0.5, 0.0))
        base = g["baseline"].dropna().unique()
        sub = f"{problem}, n = {n}. T1 = bản tuần tự nhanh nhất cùng thí nghiệm: {'; '.join(base)}. " \
              f"Thanh lỗi: IQR của Tp."
        out.append(_save(fig, fig_dir / exp / f"scaling_n{n}.png", f"{exp}: tăng tốc theo số thread", sub,
                         legend_rows=-(-len(names) // ncol)))
    return out


def plot_block_sweep(s: pd.DataFrame, fig_dir: Path) -> list[Path]:
    """GFLOP/s theo block size, moi n mot duong (chuong trinh tuan tu quet b, vi du E3)."""
    out = []
    for (exp, build, program, variant), g in s.groupby(["experiment", "build", "program", "variant"]):
        if not is_sequential(program) or g["block"].nunique() < 3:
            continue
        ns = sorted(g["n"].unique())
        if len(ns) > len(SERIES):
            continue
        fig, axes = _new_figure(1, 1, 8.0, 5.0)
        ax = axes[0][0]
        bs = sorted(int(b) for b in g["block"].unique() if b not in ns)   # b = n ve o cot rieng
        x_whole = bs[-1] * 2                                              # cot "b = n" o mep phai
        for i, n in enumerate(ns):
            d = g[(g["n"] == n) & (g["block"] < n)].sort_values("block")
            flop = 2.0 * float(n) ** 3
            y = flop / d["t_compute_median"] / 1e9
            lo = flop / d["t_compute_q3"] / 1e9
            hi = flop / d["t_compute_q1"] / 1e9
            best = d.loc[y.idxmax()]
            _line(ax, d["block"].to_numpy(), y.to_numpy(), SERIES[i],
                  f"n = {n} (nhanh nhất ở b = {int(best['block'])})", lo.to_numpy(), hi.to_numpy())
            ax.plot([best["block"]], [y.max()], marker="o", markersize=11, markerfacecolor="none",
                    markeredgecolor=SERIES[i], markeredgewidth=1.5)
            whole = g[(g["n"] == n) & (g["block"] >= n)]
            if not whole.empty:
                gw = flop / whole["t_compute_median"].iloc[0] / 1e9
                ax.plot([x_whole], [gw], marker="o", markersize=6, color=SERIES[i], markeredgecolor=SURFACE,
                        markeredgewidth=1.5, linestyle="none")
        ax.set_xscale("log", base=2)
        ax.set_xticks(bs + [x_whole])
        ax.set_xticklabels([str(b) for b in bs] + ["b = n"], rotation=45)
        ax.axvline((bs[-1] * x_whole) ** 0.5, color=GRID, linewidth=1.5)   # vach ngan cot "b = n"
        ax.set_ylim(0, None)
        _style(ax, "Block size b (thang log2)", "GFLOP/s (median)")
        ax.legend(frameon=False, fontsize=8, labelcolor=TEXT, loc="upper right")
        title = f"{exp}: tốc độ theo block size"
        sub = f"{program} --variant {variant}, 1 thread. Vòng tròn rỗng: b nhanh nhất. Cột phải: b = n " \
              f"(không chia block). Thanh lỗi: IQR."
        suffix = "" if build == "default" else f"_{build}"
        out.append(_save(fig, fig_dir / exp / f"block_sweep_{program}_{variant}{suffix}.png", title, sub))
    return out


def _time_unit(tmax: float) -> tuple[float, str]:
    """He so va don vi de truc thoi gian de doc: s, ms hoac µs."""
    if tmax >= 1:
        return 1.0, "s"
    if tmax >= 1e-3:
        return 1e3, "ms"
    return 1e6, "µs"


def _fmt_time(t: float) -> str:
    k, unit = _time_unit(t)
    return f"{t * k:.3g} {unit}".replace(".", ",")


def plot_variant_bars(s: pd.DataFrame, fig_dir: Path) -> list[Path]:
    """Thoi gian theo bien the, moi n mot o (chuong trinh tuan tu co nhieu bien the, vi du E2)."""
    out = []
    for (exp, build, program), g in s.groupby(["experiment", "build", "program"]):
        if not is_sequential(program) or g["variant"].nunique() < 2 or g["block"].nunique() > 1:
            continue
        variants = sorted(g["variant"].unique())
        if len(variants) > len(SERIES):
            continue
        ns = sorted(g["n"].unique())
        fig, axes = _new_figure(1, len(ns), 3.6 * len(ns), 4.4)
        for c, n in enumerate(ns):
            ax = axes[0][c]
            d = g[g["n"] == n].set_index("variant").reindex(variants)
            x = np.arange(len(variants))
            k, unit = _time_unit(float(d["t_compute_max"].max()))
            med = d["t_compute_median"].to_numpy()
            yerr = np.vstack([med - d["t_compute_min"].to_numpy(), d["t_compute_max"].to_numpy() - med]) * k
            ax.bar(x, med * k, width=0.6, color=[SERIES[i] for i in range(len(variants))], edgecolor=SURFACE,
                   linewidth=2)
            ax.errorbar(x, med * k, yerr=yerr, fmt="none", ecolor=TEXT_2, elinewidth=1, capsize=2.5)
            top = float(d["t_compute_max"].max()) * k
            for xi, m, mx in zip(x, med, d["t_compute_max"].to_numpy()):
                ax.text(xi, mx * k + top * 0.02, _fmt_time(m), ha="center", va="bottom", fontsize=7, color=TEXT,
                        rotation=0 if len(variants) <= 6 else 90)
            ax.set_xticks(x)
            ax.set_xticklabels(variants, fontsize=9)
            ax.set_ylim(0, top * 1.15)
            _style(ax, "", f"t_compute ({unit}, median)")
            ax.set_title(f"n = {n}", fontsize=10, color=TEXT, loc="left")
        suffix = "" if build == "default" else f"_{build}"
        out.append(_save(fig, fig_dir / exp / f"variants_{program}{suffix}.png", f"{exp}: thời gian theo biến thể",
                         f"{program}, 1 thread. Mỗi ô một kích thước, trục y riêng. Thanh lỗi: min–max của "
                         f"các lần chạy."))
    return out


def make_figures(summary: pd.DataFrame, speedup: pd.DataFrame, fig_dir: Path) -> list[Path]:
    import matplotlib
    matplotlib.use("Agg")
    paths = []
    paths += plot_variant_bars(summary, fig_dir)
    paths += plot_block_sweep(summary, fig_dir)
    paths += plot_scaling(speedup, fig_dir)
    return paths
