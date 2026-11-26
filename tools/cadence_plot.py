#!/usr/bin/env python3
"""
cadence_plot.py — 把 cadence.py 的逐週期 CSV 畫成 W13 的四張圖

用途
    W12 只有摘要（平均 2.6 ms、最差 76.4 ms），平均值把分布藏起來了。
    這支把 cadence.py 的原始資料畫成四張圖，回答「73.49 ms 長尾從哪來」。

四張圖
    ① 延遲分布直方圖（y 軸取 log）   為什麼平均 1.14 ms 卻最差 73.49 ms
    ② 百分位數表                     取代「平均／最差」這種弱摘要
    ③ 延遲對時間的散點圖（雙面板）   ★ 診斷力最強
    ④ 四條件箱型圖並排               負載與運動會不會影響通訊

③ 為什麼分兩個面板
    上panel：迴圈工作時間 work_ns —— 迴圈「自己慢」
    下panel：被搶走的時間 steal   —— 迴圈「被 OS 搶走」
        steal[i] = (t_start[i+1] - t_start[i]) - work[i] - sleep[i]
    兩者的平均週期一樣，成因完全不同。分開畫才看得出是哪一種。

    尖峰的形狀就是診斷：
        等間隔出現   → 作業系統或 USB 的排程週期
        隨機散布     → 驅動層的重試或逾時
        集中在開頭   → 暖機或快取未命中
    程式會自動量尖峰間隔的變異係數（CV），CV 小代表等間隔。

資料清理
    temp_read == 1 的週期一律排除。讀溫度會多一次匯流排交易
    （實測 work 從 2.6 ms 升到 3.4 ms），不排除的話每 600 個 tick
    就污染一個樣本，剛好落在最在意的尾端。

用法
    python tools/cadence_plot.py snapshots/cadence/*.csv
    python tools/cadence_plot.py snapshots/cadence/ --out docs/figures/w13
    python tools/cadence_plot.py a.csv b.csv --field teleop_ns

輸出
    <out>/01_hist.png          各條件的分布疊圖
    <out>/02_percentile.md     百分位數表（可直接貼進 lab log 或簡報）
    <out>/03_scatter_<cond>.png 每個條件一張雙面板散點圖
    <out>/04_box.png           四條件並排
    終端機同時印出百分位數表與尖峰週期性分析
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib import font_manager, rcParams  # noqa: E402

SCRIPT_VERSION = "1.0 (2026-11-27)"

# 用 Week9／Week12 簡報的同一組配色，圖表貼進投影片不會突兀
C_MAIN = "#3A5F7D"
C_ACCENT = "#EF4919"
C_MID = "#82A4BE"
C_TINT = "#E1E9F5"
C_MUTED = "#6B7C8C"
PALETTE = [C_MAIN, C_MID, "#9C7BB5", "#5C8A72", C_ACCENT]

COLUMNS = ["tick", "t_start_ns", "observe_ns", "teleop_ns",
           "send_ns", "work_ns", "sleep_ns", "temp_read"]

# 圖上用的名稱。欄位名帶 _ns 但畫出來是毫秒，直接拿來當軸標籤會矛盾。
LABEL = {
    "work_ns": "迴圈工作時間",
    "observe_ns": "讀 follower 六軸（observe）",
    "teleop_ns": "讀 leader 六軸（teleop）",
    "send_ns": "寫 follower 六軸（send）",
    "sleep_ns": "睡眠",
}
IDX = {name: i for i, name in enumerate(COLUMNS)}

def _log_ticks(ax, axis: str = "y") -> None:
    """log 軸印 0.2、1、3、70，而不是 6 × 10⁰。"""
    from matplotlib.ticker import FuncFormatter, LogLocator
    target = ax.yaxis if axis == "y" else ax.xaxis
    target.set_major_locator(LogLocator(base=10, subs=(1.0, 2.0, 5.0), numticks=12))
    target.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    target.set_minor_formatter(FuncFormatter(lambda v, _: ""))


CJK_FONTS = ["Microsoft JhengHei", "Noto Sans CJK TC", "Noto Sans TC",
             "PingFang TC", "Source Han Sans TW", "SimHei", "Microsoft YaHei"]


def setup_font() -> None:
    have = {f.name for f in font_manager.fontManager.ttflist}
    for name in CJK_FONTS:
        if name in have:
            rcParams["font.family"] = name
            break
    else:
        print("[warn] 找不到中文字型，圖上的中文可能變成方框。",
              file=sys.stderr)
        print("       Windows 通常有 Microsoft JhengHei；"
              "若沒有請安裝思源黑體後重跑。", file=sys.stderr)
    rcParams["axes.unicode_minus"] = False
    rcParams["figure.dpi"] = 150
    rcParams["savefig.bbox"] = "tight"
    rcParams["axes.spines.top"] = False
    rcParams["axes.spines.right"] = False
    rcParams["axes.edgecolor"] = C_MUTED
    rcParams["axes.labelcolor"] = C_MAIN
    rcParams["text.color"] = C_MAIN
    rcParams["xtick.color"] = C_MUTED
    rcParams["ytick.color"] = C_MUTED


class Run:
    """一個條件的一次實驗。"""

    def __init__(self, path: Path) -> None:
        self.path = path
        raw = np.loadtxt(path, delimiter=",", skiprows=1, dtype=np.int64)
        if raw.ndim == 1:
            raw = raw.reshape(1, -1)
        self.raw = raw
        # 排除讀溫度的週期
        self.data = raw[raw[:, IDX["temp_read"]] == 0]
        self.name = self._name_from(path)

    @staticmethod
    def _name_from(path: Path) -> str:
        # 20261130_143022_condA-idle_60hz.csv → condA-idle
        parts = path.stem.split("_")
        for p in parts:
            if p.startswith("cond"):
                return p
        return path.stem

    def ms(self, field: str) -> np.ndarray:
        return self.data[:, IDX[field]] / 1e6

    def seconds(self) -> np.ndarray:
        return self.data[:, IDX["t_start_ns"]] / 1e9

    def steal_ms(self) -> np.ndarray:
        """被 OS 搶走的時間：週期間隔扣掉迴圈工作與睡眠。

        用原始陣列算（不排除溫度週期），否則相鄰關係會斷掉；
        算完再把溫度週期的值挖掉。
        """
        t = self.raw[:, IDX["t_start_ns"]]
        work = self.raw[:, IDX["work_ns"]]
        sleep = self.raw[:, IDX["sleep_ns"]]
        if len(t) < 2:
            return np.array([])
        steal = np.diff(t) - work[:-1] - sleep[:-1]
        keep = self.raw[:-1, IDX["temp_read"]] == 0
        return steal[keep] / 1e6

    def steal_seconds(self) -> np.ndarray:
        t = self.raw[:, IDX["t_start_ns"]]
        keep = self.raw[:-1, IDX["temp_read"]] == 0
        return t[:-1][keep] / 1e9

    def percentiles(self, field: str) -> dict:
        v = self.ms(field)
        if len(v) == 0:
            return {}
        return {
            "n": len(v),
            "mean": float(v.mean()),
            "p50": float(np.percentile(v, 50)),
            "p90": float(np.percentile(v, 90)),
            "p99": float(np.percentile(v, 99)),
            "p99.9": float(np.percentile(v, 99.9)),
            "max": float(v.max()),
        }

    def effective_hz(self) -> float | None:
        t = self.seconds()
        if len(t) < 2:
            return None
        span = t[-1] - t[0]
        return (len(t) - 1) / span if span > 0 else None


def spike_periodicity(times_s: np.ndarray, values_ms: np.ndarray,
                      factor: float = 5.0) -> dict:
    """找出尖峰並量它們的間隔是否規律。

    CV（變異係數 = 標準差 / 平均）是判斷依據：
        CV < 0.2  → 高度規律，指向作業系統或 USB 的固定排程
        CV > 0.8  → 隨機，指向驅動層的重試或逾時
    """
    if len(values_ms) == 0:
        return {}
    median = float(np.median(values_ms))
    threshold = median * factor
    idx = np.where(values_ms > threshold)[0]
    out = {
        "median_ms": round(median, 4),
        "threshold_ms": round(threshold, 4),
        "spike_count": int(len(idx)),
    }
    if len(idx) < 3:
        out["verdict"] = "尖峰太少，看不出規律"
        return out
    gaps = np.diff(times_s[idx])
    mean_gap = float(gaps.mean())
    std_gap = float(gaps.std())
    cv = std_gap / mean_gap if mean_gap > 0 else float("inf")
    out.update({
        "mean_gap_s": round(mean_gap, 4),
        "std_gap_s": round(std_gap, 4),
        "cv": round(cv, 3),
    })
    if cv < 0.2:
        out["verdict"] = f"高度規律（每 {mean_gap:.3f} 秒一次）→ 指向 OS／USB 的固定排程"
    elif cv < 0.5:
        out["verdict"] = "偏規律 → 可能是排程，但有其他因素疊加"
    elif cv < 0.8:
        out["verdict"] = "不太規律 → 成因可能混合"
    else:
        out["verdict"] = "隨機 → 指向驅動層的重試或逾時"
    return out


# ---------------------------------------------------------------------------
# 四張圖
# ---------------------------------------------------------------------------


def fig_hist(runs: list[Run], field: str, out: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 4.5))
    for i, r in enumerate(runs):
        v = r.ms(field)
        if len(v) == 0:
            continue
        ax.hist(v, bins=120, histtype="step", linewidth=1.6,
                color=PALETTE[i % len(PALETTE)], label=f"{r.name}（n={len(v)}）")
    ax.set_yscale("log")
    _log_ticks(ax)
    ax.set_xlabel(f"{LABEL.get(field, field)} / 毫秒")
    ax.set_ylabel("次數（對數）")
    ax.set_title(f"① {LABEL.get(field, field)}的分布　y 軸取對數才看得到長尾")
    ax.legend(frameon=False, fontsize=9)
    ax.grid(axis="y", color=C_TINT, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.savefig(out)
    plt.close(fig)


def fig_scatter(run: Run, field: str, out: Path) -> dict:
    t = run.seconds()
    v = run.ms(field)
    st_t = run.steal_seconds()
    st = run.steal_ms()

    fig, (ax1, ax2) = plt.subplots(
        2, 1, figsize=(10, 6.5), sharex=True,
        gridspec_kw={"height_ratios": [1, 1], "hspace": 0.18},
    )

    ax1.scatter(t, v, s=3, color=C_MAIN, alpha=0.35, linewidths=0)
    ax1.set_yscale("log")
    _log_ticks(ax1)
    ax1.set_ylabel(f"{LABEL.get(field, field)} / 毫秒")
    ax1.set_title(f"③ {run.name}　上：迴圈自己慢　下：迴圈被 OS 搶走")
    ax1.grid(color=C_TINT, linewidth=0.8)
    ax1.set_axisbelow(True)

    if len(st):
        pos = np.clip(st, 1e-4, None)  # log 軸不能有 0 或負數
        ax2.scatter(st_t, pos, s=3, color=C_ACCENT, alpha=0.35, linewidths=0)
        ax2.set_yscale("log")
    if len(st):
        _log_ticks(ax2)
    ax2.set_xlabel("時間 / 秒")
    ax2.set_ylabel("被搶走 / 毫秒")
    ax2.grid(color=C_TINT, linewidth=0.8)
    ax2.set_axisbelow(True)

    fig.savefig(out)
    plt.close(fig)

    return {
        "work": spike_periodicity(t, v),
        "steal": spike_periodicity(st_t, st) if len(st) else {},
    }


def fig_box(runs: list[Run], field: str, out: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 4.5))
    data = [r.ms(field) for r in runs if len(r.ms(field))]
    names = [r.name for r in runs if len(r.ms(field))]
    if not data:
        plt.close(fig)
        return
    # 不用 labels= 也不用 tick_labels=：前者在 matplotlib 3.9 之後被淘汰，
    # 後者在 3.9 之前不存在。事後設 xticks 兩邊都通。
    bp = ax.boxplot(data, showfliers=True, patch_artist=True,
                    flierprops={"marker": ".", "markersize": 2,
                                "markerfacecolor": C_ACCENT,
                                "markeredgecolor": "none", "alpha": 0.4},
                    medianprops={"color": C_ACCENT, "linewidth": 1.6},
                    whiskerprops={"color": C_MUTED},
                    capprops={"color": C_MUTED})
    ax.set_xticks(range(1, len(names) + 1))
    ax.set_xticklabels(names)
    for patch in bp["boxes"]:
        patch.set_facecolor(C_TINT)
        patch.set_edgecolor(C_MAIN)
    ax.set_yscale("log")
    _log_ticks(ax)
    ax.set_ylabel(f"{LABEL.get(field, field)} / 毫秒")
    ax.set_title(f"④ 各條件的{LABEL.get(field, field)}比較　箱體相同代表條件不影響通訊")
    ax.grid(axis="y", color=C_TINT, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.savefig(out)
    plt.close(fig)


def table_percentiles(runs: list[Run], fields: list[str]) -> str:
    lines = ["# ② 百分位數", ""]
    for field in fields:
        lines += [f"## {field}", "",
                  "| 條件 | n | 平均 | p50 | p90 | p99 | p99.9 | max |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for r in runs:
            s = r.percentiles(field)
            if not s:
                continue
            lines.append(
                f"| {r.name} | {s['n']} | {s['mean']:.3f} | {s['p50']:.3f} | "
                f"{s['p90']:.3f} | {s['p99']:.3f} | {s['p99.9']:.3f} | "
                f"{s['max']:.3f} |"
            )
        lines.append("")
    lines += ["單位：毫秒。已排除 temp_read = 1 的週期。", ""]
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="把 cadence.py 的 CSV 畫成 W13 的四張圖")
    ap.add_argument("inputs", nargs="+", help="CSV 檔或含 CSV 的資料夾")
    ap.add_argument("--field", default="work_ns",
                    choices=["work_ns", "observe_ns", "teleop_ns", "send_ns"],
                    help="主要分析的欄位，預設 work_ns")
    ap.add_argument("--out", default="docs/figures/w13", help="輸出資料夾")
    ap.add_argument("--spike-factor", type=float, default=5.0,
                    help="尖峰門檻 = 中位數的幾倍，預設 5")
    args = ap.parse_args(argv)

    paths: list[Path] = []
    for item in args.inputs:
        p = Path(item)
        if p.is_dir():
            paths.extend(sorted(p.glob("*.csv")))
        elif p.exists():
            paths.append(p)
        else:
            print(f"[warn] 找不到 {p}", file=sys.stderr)
    if not paths:
        print("沒有可讀的 CSV", file=sys.stderr)
        return 1

    setup_font()
    runs = [Run(p) for p in paths]
    runs.sort(key=lambda r: r.name)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    fig_hist(runs, args.field, out_dir / "01_hist.png")
    fig_box(runs, args.field, out_dir / "04_box.png")

    table = table_percentiles(runs, [args.field, "observe_ns",
                                     "teleop_ns", "send_ns"])
    (out_dir / "02_percentile.md").write_text(table, encoding="utf-8")

    print(table)
    print("=" * 64)
    print("尖峰週期性分析（③ 散點圖的量化版本）\n")

    for r in runs:
        res = fig_scatter(r, args.field, out_dir / f"03_scatter_{r.name}.png")
        hz = r.effective_hz()
        print(f"[{r.name}]  實際頻率 {hz:.2f} Hz" if hz else f"[{r.name}]")
        for key, label in (("work", "迴圈自己慢"), ("steal", "被 OS 搶走")):
            s = res.get(key) or {}
            if not s:
                continue
            line = (f"  {label}：中位數 {s['median_ms']:.3f} ms，"
                    f"超過 {s['threshold_ms']:.3f} ms 的尖峰 {s['spike_count']} 次")
            if "cv" in s:
                line += f"，間隔 {s['mean_gap_s']:.3f}±{s['std_gap_s']:.3f} s（CV {s['cv']}）"
            print(line)
            print(f"    → {s['verdict']}")
        print()

    print(f"圖與表已寫入 {out_dir}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
