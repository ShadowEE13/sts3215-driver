#!/usr/bin/env python3
"""
cadence.py — 控制迴圈逐週期計時紀錄（W13 定量實驗）

用途
    以固定頻率跑 observe → teleop → send 三段匯流排動作，把「每一個週期」的
    耗時原封不動記下來，供事後畫分布圖、百分位數與散點圖。

    W12 的 688 秒實測只拿到摘要（平均 2.6 ms、最差 76.4 ms），平均值把分布
    藏起來了。73.49 ms 的長尾是分布問題不是平均值問題，要回答「它從哪來」
    必須有逐週期的原始資料。

量測不干擾原則（這支工具的核心限制）
    1. 迴圈中不寫檔。全部進預配置的 numpy 陣列，跑完才一次寫出。
       若用有緩衝的檔案物件，緩衝滿了會自動 flush，製造出「等間隔尖峰」——
       那正是我們要找的 OS 排程特徵，會得到自信但錯誤的結論。
    2. 迴圈中不配置記憶體。list.append 容量滿了會重新配置，同樣是週期性尖峰。
    3. 迴圈中不做單位換算。perf_counter_ns() 回傳整數奈秒就存整數奈秒。
    4. 溫度讀取會多一次匯流排交易，所以該週期用 temp_read 欄位標記，
       分析時可以排除，不會污染延遲分布。

時鐘（延續 F-22、F-26 的結論）
    間隔一律用 time.perf_counter_ns()：單調遞增、高解析度、不受 NTP 對時影響。
    牆鐘只在開跑前呼叫一次 time.time_ns() 當錨點，寫進 metadata，
    讓事後可以還原真實時間，但迴圈中不碰會被調整的時鐘。

四個實驗條件
    A 待機      迴圈照跑，但人不動 leader
    B 單軸      只緩慢移動一個關節
    C 六軸      正常遙控（對應 W12 的 688 秒）
    D 負載      夾持物體或手臂伸直

    注意：這四個條件的「程式碼完全相同」，差別只在人怎麼操作。
    這是刻意的——程式路徑一致，唯一的變數才是物理條件，對照才成立。
    --condition 只是標籤，不改變任何行為。

用法
    # 先驗證 logger 本身（不需硬體，不 import LeRobot）
    python tools/cadence.py --port fake --condition A --label logger-check --seconds 60

    # 真機，四個條件各跑三分鐘
    python tools/cadence.py --port COM5 --leader-port COM6 --condition A --label idle
    python tools/cadence.py --port COM5 --leader-port COM6 --condition B --label single-axis
    python tools/cadence.py --port COM5 --leader-port COM6 --condition C --label full
    python tools/cadence.py --port COM5 --leader-port COM6 --condition D --label load

輸出
    snapshots/cadence/<日期>_<時間>_cond<X>-<label>_<hz>hz.csv    逐週期，一列一個 tick
    snapshots/cadence/<日期>_<時間>_cond<X>-<label>_<hz>hz.json   條件、環境、溫度、摘要

CSV 欄位（全部是整數奈秒，除了 tick 與 temp_read）
    tick          第幾個週期，從 0 起。跳號代表有 tick 沒記到
    t_start_ns    這個週期開始的時間，相對於開跑那一刻
    observe_ns    讀 follower 六軸
    teleop_ns     讀 leader 六軸
    send_ns       寫 follower 六軸
    work_ns       迴圈本體總時間（含上面三段與之間的開銷）
    sleep_ns      實際睡了多久（不是要求的，是量到的）
    temp_read     1 = 這個週期有多讀一次溫度，分析時建議排除

    為什麼要存絕對的 t_start_ns：
    只記「每段花多久」無法區分「迴圈自己慢」與「迴圈被 OS 搶走」。
    前者 work_ns 變大，後者 work_ns 正常但兩個 t_start_ns 的差變大。
    兩者平均週期一樣，成因完全不同。
    殘差 = (t_start[i+1] - t_start[i]) - work[i] - sleep[i]，就是被搶走的時間。

安全
    本工具不寫 EEPROM，只做與 LeRobot teleoperate 相同的三種動作。
    但扭矩是開啟的，手臂會動：第一次跑之前電源開關放手邊。
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

SCRIPT_VERSION = "1.0 (2026-11-27：W13 定量實驗)"

COLUMNS = [
    "tick",
    "t_start_ns",
    "observe_ns",
    "teleop_ns",
    "send_ns",
    "work_ns",
    "sleep_ns",
    "temp_read",
]

CONDITIONS = {
    "A": "待機：迴圈照跑，人不動 leader",
    "B": "單軸：只緩慢移動一個關節",
    "C": "六軸：正常遙控",
    "D": "負載：夾持物體或手臂伸直",
}


# ---------------------------------------------------------------------------
# 裝置介面
#
# 這一段是唯一與 LeRobot 耦合的地方，三個方法名稱請對照你的 LeRobot 版本
# （ba83e4cf）確認。其餘程式完全不依賴 LeRobot。
# ---------------------------------------------------------------------------


class LeRobotDevices:
    """包住 LeRobot 的 follower / leader，只暴露四個動作。"""

    def __init__(self, follower_port: str, leader_port: str,
                 follower_id: str, leader_id: str) -> None:
        # ↓↓↓ 要確認的部分：模組路徑與類別名稱因 LeRobot 版本而異 ↓↓↓
        from lerobot.robots.so101_follower import SO101Follower, SO101FollowerConfig
        from lerobot.teleoperators.so101_leader import SO101Leader, SO101LeaderConfig

        self.follower = SO101Follower(
            SO101FollowerConfig(port=follower_port, id=follower_id)
        )
        self.leader = SO101Leader(
            SO101LeaderConfig(port=leader_port, id=leader_id)
        )
        # ↑↑↑ 要確認的部分 ↑↑↑

        self.follower.connect()
        self.leader.connect()
        self._temp_warned = False

    def observe(self):
        return self.follower.get_observation()

    def teleop(self):
        return self.leader.get_action()

    def send(self, action) -> None:
        self.follower.send_action(action)

    def temps(self):
        """回傳 {馬達名稱: 溫度}；讀不到就回 None（只警告一次）。"""
        try:
            return self.follower.bus.sync_read("Present_Temperature")
        except Exception as exc:  # noqa: BLE001 — 溫度失敗不該中斷實驗
            if not self._temp_warned:
                print(f"[warn] 讀不到溫度，後續不再提示：{exc}", file=sys.stderr)
                self._temp_warned = True
            return None

    def close(self) -> None:
        for dev in (self.leader, self.follower):
            try:
                dev.disconnect()
            except Exception:  # noqa: BLE001
                pass


class FakeDevices:
    """不碰硬體的替身，用來驗證 logger 本身不會製造尖峰。

    三段耗時刻意貼近 W12 的實測中位數（1.2 / 1.1 / 0.2 ms），
    這樣跑出來的圖如果是平的，代表尖峰不是 logger 造成的。
    """

    def __init__(self) -> None:
        self._n = 0

    @staticmethod
    def _spin(seconds: float) -> None:
        # 用忙等而非 sleep：sleep 本身的誤差正是我們要量的東西，
        # 替身不該把它混進來。
        end = time.perf_counter() + seconds
        while time.perf_counter() < end:
            pass

    def observe(self):
        self._spin(0.00123)
        return {"fake": self._n}

    def teleop(self):
        self._spin(0.00114)
        self._n += 1
        return {"fake": self._n}

    def send(self, action) -> None:
        self._spin(0.00021)

    def temps(self):
        self._spin(0.0008)
        return {f"motor_{i}": 30.0 for i in range(1, 7)}

    def close(self) -> None:
        pass


# ---------------------------------------------------------------------------
# 量測迴圈
# ---------------------------------------------------------------------------


def run(devices, hz: float, seconds: float, temp_every_s: float):
    """跑固定頻率的量測迴圈，回傳 (逐週期陣列, 溫度紀錄)。"""
    period_ns = round(1e9 / hz)
    duration_ns = round(seconds * 1e9)
    temp_every_ns = round(temp_every_s * 1e9) if temp_every_s > 0 else 0

    # 預配置：多給 20% 再加 100，避免跑超過預期長度時溢位。
    capacity = int(seconds * hz * 1.2) + 100
    buf = np.zeros((capacity, len(COLUMNS)), dtype=np.int64)

    # 溫度一次 10 秒才記一次，三分鐘約 18 筆，用 list 不會造成可觀測的影響。
    temp_log: list[dict] = []

    t0 = time.perf_counter_ns()
    next_deadline = t0 + period_ns
    last_temp_ns = -temp_every_ns  # 讓第 0 個週期就記一次
    i = 0
    interrupted = False

    try:
        while i < capacity:
            ts = time.perf_counter_ns()
            if ts - t0 >= duration_ns:
                break

            a = time.perf_counter_ns()
            devices.observe()
            b = time.perf_counter_ns()
            action = devices.teleop()
            c = time.perf_counter_ns()
            devices.send(action)
            d = time.perf_counter_ns()

            did_temp = 0
            if temp_every_ns and (ts - t0) - last_temp_ns >= temp_every_ns:
                values = devices.temps()
                last_temp_ns = ts - t0
                did_temp = 1
                if values is not None:
                    temp_log.append({"tick": i, "t_ns": ts - t0, "temps": values})

            work_end = time.perf_counter_ns()
            work_ns = work_end - ts

            # 絕對截止時間排程：誤差不累積。
            remaining_ns = next_deadline - time.perf_counter_ns()
            if remaining_ns > 0:
                time.sleep(remaining_ns / 1e9)
            sleep_ns = time.perf_counter_ns() - work_end

            # 寫入在 sleep 之後：這一行的耗時落在下一個週期的間隙裡，
            # 而間隙本身可以由 t_start 的差值算出來，不會被藏起來。
            buf[i] = (i, ts - t0, b - a, c - b, d - c, work_ns, sleep_ns, did_temp)

            next_deadline += period_ns
            i += 1

    except KeyboardInterrupt:
        interrupted = True
        print(f"\n[info] 中斷，已記錄 {i} 個週期，仍會寫檔。", file=sys.stderr)

    if i >= capacity:
        print(f"[warn] 達到預配置上限 {capacity}，提早結束。", file=sys.stderr)

    return buf[:i], temp_log, interrupted


# ---------------------------------------------------------------------------
# 輸出
# ---------------------------------------------------------------------------


def summarise(data: np.ndarray) -> dict:
    """算百分位數。排除有讀溫度的週期，那些不具代表性。"""
    if len(data) == 0:
        return {}
    clean = data[data[:, COLUMNS.index("temp_read")] == 0]
    out: dict = {
        "ticks_total": int(len(data)),
        "ticks_used": int(len(clean)),
        "ticks_with_temp_read": int(len(data) - len(clean)),
    }
    for name in ("observe_ns", "teleop_ns", "send_ns", "work_ns", "sleep_ns"):
        col = clean[:, COLUMNS.index(name)]
        if len(col) == 0:
            continue
        out[name] = {
            "mean_ms": round(float(col.mean()) / 1e6, 4),
            "p50_ms": round(float(np.percentile(col, 50)) / 1e6, 4),
            "p90_ms": round(float(np.percentile(col, 90)) / 1e6, 4),
            "p99_ms": round(float(np.percentile(col, 99)) / 1e6, 4),
            "p99_9_ms": round(float(np.percentile(col, 99.9)) / 1e6, 4),
            "max_ms": round(float(col.max()) / 1e6, 4),
        }
    # 實際頻率與週期間隔
    t = clean[:, COLUMNS.index("t_start_ns")]
    if len(t) > 1:
        span_s = float(t[-1] - t[0]) / 1e9
        out["effective_hz"] = round((len(t) - 1) / span_s, 3) if span_s > 0 else None
        gaps = np.diff(t)
        out["tick_interval_ms"] = {
            "p50": round(float(np.percentile(gaps, 50)) / 1e6, 4),
            "p99": round(float(np.percentile(gaps, 99)) / 1e6, 4),
            "max": round(float(gaps.max()) / 1e6, 4),
        }
    return out


def git_rev() -> str | None:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=Path(__file__).resolve().parent,
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except Exception:  # noqa: BLE001
        return None


def lerobot_version() -> str | None:
    try:
        import lerobot  # noqa: PLC0415
        return getattr(lerobot, "__version__", "unknown")
    except Exception:  # noqa: BLE001
        return None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="控制迴圈逐週期計時紀錄（W13 定量實驗）")
    ap.add_argument("--port", required=True, help="follower 的 COM port；或 fake（不碰硬體）")
    ap.add_argument("--leader-port", default=None, help="leader 的 COM port，--port fake 時可省略")
    ap.add_argument("--condition", required=True, choices=sorted(CONDITIONS),
                    help="A 待機 / B 單軸 / C 六軸 / D 負載。只是標籤，不改變行為")
    ap.add_argument("--label", required=True, help="檔名標籤，例如 idle、full、load")
    ap.add_argument("--hz", type=float, default=60.0)
    ap.add_argument("--seconds", type=float, default=180.0, help="預設 180 秒（約 11000 個週期）")
    ap.add_argument("--temp-every", type=float, default=10.0, help="每幾秒讀一次溫度，0 = 不讀")
    ap.add_argument("--follower-id", default="my_follower")
    ap.add_argument("--leader-id", default="my_leader")
    ap.add_argument("--note", default="", help="自由備註，例如『夾持 200 g、室溫 24 度』")
    ap.add_argument("--out", default=None, help="輸出資料夾，預設 repo 的 snapshots/cadence/")
    args = ap.parse_args(argv)

    if args.port == "fake":
        devices = FakeDevices()
    else:
        if not args.leader_port:
            ap.error("真機模式需要 --leader-port")
        devices = LeRobotDevices(
            args.port, args.leader_port, args.follower_id, args.leader_id
        )

    started = _dt.datetime.now().astimezone()
    wall_anchor_ns = time.time_ns()

    print(f"條件 {args.condition}：{CONDITIONS[args.condition]}")
    print(f"目標 {args.hz} Hz，跑 {args.seconds} 秒，約 {int(args.hz * args.seconds)} 個週期")
    print("開始（Ctrl-C 可提前結束，已記錄的資料仍會寫檔）\n")

    try:
        data, temp_log, interrupted = run(
            devices, args.hz, args.seconds, args.temp_every
        )
    finally:
        devices.close()

    out_dir = Path(args.out) if args.out else \
        Path(__file__).resolve().parent.parent / "snapshots" / "cadence"
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = (f"{started:%Y%m%d_%H%M%S}_cond{args.condition}"
            f"-{args.label}_{int(args.hz)}hz")

    csv_path = out_dir / f"{stem}.csv"
    np.savetxt(csv_path, data, fmt="%d", delimiter=",",
               header=",".join(COLUMNS), comments="")

    summary = summarise(data)
    meta = {
        "script": "cadence.py",
        "script_version": SCRIPT_VERSION,
        "git_rev": git_rev(),
        "started": started.isoformat(),
        "wall_anchor_ns": wall_anchor_ns,
        "interrupted": interrupted,
        "condition": args.condition,
        "condition_desc": CONDITIONS[args.condition],
        "label": args.label,
        "note": args.note,
        "target_hz": args.hz,
        "target_seconds": args.seconds,
        "temp_every_s": args.temp_every,
        "ports": {"follower": args.port, "leader": args.leader_port},
        "ids": {"follower": args.follower_id, "leader": args.leader_id},
        "fake": args.port == "fake",
        "env": {
            "python": sys.version.split()[0],
            "platform": platform.platform(),
            "numpy": np.__version__,
            "lerobot": lerobot_version(),
        },
        "clock": "time.perf_counter_ns() 量間隔；time.time_ns() 只取一次當錨點",
        "columns": COLUMNS,
        "summary": summary,
        "temperatures": temp_log,
    }
    json_path = out_dir / f"{stem}.json"
    json_path.write_text(
        json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    print(f"\n寫出 {len(data)} 個週期")
    print(f"  {csv_path}")
    print(f"  {json_path}")
    if summary:
        print(f"\n實際頻率 {summary.get('effective_hz')} Hz")
        for name in ("observe_ns", "teleop_ns", "send_ns"):
            s = summary.get(name)
            if s:
                print(f"  {name:12s} p50 {s['p50_ms']:>7.3f} ms   "
                      f"p99 {s['p99_ms']:>7.3f} ms   max {s['max_ms']:>8.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
