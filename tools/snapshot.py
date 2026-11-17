#!/usr/bin/env python3
"""
snapshot.py — STS3215 暫存器唯讀快照 + READ 往返時間量測

用途
    新組裝的手臂在 LeRobot calibrate / connect 之前，把每顆馬達的 Control Table
    完整讀出存檔。LeRobot 之後會改寫 0x07（回應延遲）、0x12（Phase）、PID、
    homing offset 與角度限制；其中 Phase 的出廠值原廠表沒有列（F-17），
    過了 calibrate 就讀不回來。

唯讀保證（程式層級）
    本程式只組得出 PING（0x01）與 READ（0x02）兩種指令。
    - build_packet() 遇到其他指令直接丟 ReadOnlyViolation
    - Bus.transact() 送出前再檢查一次封包第 5 個 byte
    WRITE（0x03）、REG WRITE（0x04）、ACTION（0x05）、RESET（0x06，恢復出廠！）、
    SYNC WRITE（0x83）在程式裡沒有任何路徑可以送出。
    也不使用廣播 ID 0xFE：多顆同時回應會在匯流排上撞包。

不用 SDK 的原因
    - F-18：PyPI 的 feetech-servo-sdk 與原廠 SDK 是兩套 API
    - F-19：SDK 的 ping() 實際送兩包，量 RTT 會量到兩次往返
    - F-20：SDK 的 parser 會把轉接板的 echo 當成合法回應
    自己組包只有幾十行，每個 byte 都知道是什麼。

用法
    python tools/snapshot.py --port COM5 --label follower
    python tools/snapshot.py --port COM6 --label leader
    python tools/snapshot.py --port COM5 --label L3-pre --ids 1     # 單顆、尚未設 ID（出廠 ID 1）
    python tools/snapshot.py --port COM5 --label scan --scan        # 不確定有哪些 ID 時先掃
    python tools/snapshot.py --port fake --label test               # 在家用 fake_bus 測試
    python tools/snapshot.py --port fake:echo,missing=3 --label t   # fake_bus 故障注入

輸出
    snapshots/<日期>_<時間>_<label>.json
    每顆馬達保留原始 bytes（主要紀錄）與解碼後的暫存器（方便閱讀），外加 RTT 統計。
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path

SCRIPT_VERSION = "2.0 (2026-11-17 重寫)"

# ---------------------------------------------------------------------------
# 協定常數
# ---------------------------------------------------------------------------

INST_PING = 0x01
INST_READ = 0x02
ALLOWED_INSTRUCTIONS = {INST_PING: "PING", INST_READ: "READ"}

BROADCAST_ID = 0xFE

# 讀取範圍：EPROM、SRAM、以及原廠表沒列的 0x47 之後（V-15，LeRobot 會寫 0x50–0x55）
READ_RANGES = [
    (0x00, 40),   # 0x00–0x27 EPROM
    (0x28, 31),   # 0x28–0x46 SRAM
    (0x47, 16),   # 0x47–0x56 原廠表未列，純觀察
]
FALLBACK_CHUNK = 8  # 一次讀整段失敗時，改成每 8 bytes 一段重讀

# (位址, 長度, 名稱, sign bit 或 None)
# 依 protocol-notes.md、memory-table-check.md、LeRobot tables.py 整理
REGISTERS = [
    (0x00, 1, "Firmware_Major", None),
    (0x01, 1, "Firmware_Minor", None),
    (0x02, 1, "End", None),
    (0x03, 2, "Model_Number", None),
    (0x05, 1, "ID", None),
    (0x06, 1, "Baud_Rate", None),
    (0x07, 1, "Return_Delay_Time", None),      # 2 µs 單位，出廠 250 = 500 µs
    (0x08, 1, "Status_Return_Level", None),
    (0x09, 2, "Min_Position_Limit", None),
    (0x0B, 2, "Max_Position_Limit", None),
    (0x0D, 1, "Max_Temperature_Limit", None),
    (0x0E, 1, "Max_Voltage_Limit", None),      # 0.1 V
    (0x0F, 1, "Min_Voltage_Limit", None),      # 0.1 V
    (0x10, 2, "Max_Torque_Limit", None),
    (0x12, 1, "Phase", None),                  # F-17：BIT3 速度 0 行為、BIT4 單/多圈
    (0x13, 1, "Unloading_Condition", None),
    (0x14, 1, "LED_Alarm_Condition", None),
    (0x15, 1, "P_Coefficient", None),
    (0x16, 1, "D_Coefficient", None),
    (0x17, 1, "I_Coefficient", None),
    (0x18, 1, "Minimum_Startup_Force", None),  # 原廠表為 1 byte；LeRobot 當 2 bytes（LeRobot-notes 4.6）
    (0x19, 1, "Integral_Limit", None),
    (0x1A, 1, "CW_Dead_Zone", None),
    (0x1B, 1, "CCW_Dead_Zone", None),
    (0x1C, 2, "Protection_Current", None),
    (0x1E, 1, "Angular_Resolution", None),
    (0x1F, 2, "Homing_Offset", 11),
    (0x21, 1, "Operating_Mode", None),
    (0x22, 1, "Protective_Torque", None),
    (0x23, 1, "Protection_Time", None),
    (0x24, 1, "Overload_Torque", None),
    (0x25, 1, "Velocity_P_Coefficient", None),
    (0x26, 1, "Over_Current_Protection_Time", None),
    (0x27, 1, "Velocity_I_Coefficient", None),
    (0x28, 1, "Torque_Enable", None),
    (0x29, 1, "Acceleration", None),
    (0x2A, 2, "Goal_Position", 15),
    (0x2C, 2, "Goal_Time", None),
    (0x2E, 2, "Goal_Velocity", 15),
    (0x30, 2, "Torque_Limit", None),
    (0x37, 1, "Lock", None),
    (0x38, 2, "Present_Position", 15),
    (0x3A, 2, "Present_Velocity", 15),
    (0x3C, 2, "Present_Load", 10),
    (0x3E, 1, "Present_Voltage", None),        # 0.1 V
    (0x3F, 1, "Present_Temperature", None),
    (0x40, 1, "Async_Write_Flag", None),
    (0x41, 1, "Status", None),                 # error byte 的同一組 bit
    (0x42, 1, "Moving", None),
    (0x45, 2, "Present_Current", None),        # 6.5 mA 單位
]


class ReadOnlyViolation(Exception):
    """試圖組出 PING / READ 以外的指令。正常使用下不可能發生。"""


# ---------------------------------------------------------------------------
# 組包與解包
# ---------------------------------------------------------------------------

def checksum(body: bytes) -> int:
    """body = ID 到最後一個參數。checksum = ~(總和) 的低 8 位。"""
    return (~sum(body)) & 0xFF


def build_packet(sid: int, instruction: int, params: bytes = b"") -> bytes:
    if instruction not in ALLOWED_INSTRUCTIONS:
        raise ReadOnlyViolation(
            f"instruction 0x{instruction:02X} 不在允許清單（只允許 PING/READ）")
    if not 0 <= sid <= 253:
        raise ValueError(f"ID {sid} 不合法（0–253；不使用廣播 0xFE）")
    body = bytes([sid, len(params) + 2, instruction]) + bytes(params)
    return b"\xff\xff" + body + bytes([checksum(body)])


def try_parse(buf: bytearray, stats: dict) -> dict | None:
    """
    從 buf 開頭找出一個完整且 checksum 正確的回應包，找到就從 buf 移除並回傳。
    字頭前的雜訊丟掉；checksum 錯的包丟掉第一個 byte 重新找字頭（resync）。
    """
    while True:
        start = buf.find(b"\xff\xff")
        if start < 0:
            # 保留最後一個 0xFF，它可能是下一個字頭的前半
            keep = 1 if buf[-1:] == b"\xff" else 0
            if len(buf) > keep:
                stats["junk_bytes"] += len(buf) - keep
                del buf[: len(buf) - keep]
            return None
        if start > 0:
            stats["junk_bytes"] += start
            del buf[:start]
        if len(buf) < 4:
            return None
        length = buf[3]
        total = 4 + length
        if length < 2:
            stats["bad_packets"] += 1
            del buf[:1]
            continue
        if len(buf) < total:
            return None
        pkt = bytes(buf[:total])
        if checksum(pkt[2:-1]) != pkt[-1]:
            stats["bad_checksum"] += 1
            del buf[:1]
            continue
        del buf[:total]
        return {"id": pkt[2], "error": pkt[4], "params": pkt[5:-1], "raw": pkt}


# ---------------------------------------------------------------------------
# 匯流排
# ---------------------------------------------------------------------------

class Bus:
    def __init__(self, port, timeout_s: float = 0.05, retries: int = 3):
        self.port = port
        self.timeout_s = timeout_s
        self.retries = retries
        self.stats = {
            "packets_sent": 0, "timeouts": 0, "bad_checksum": 0,
            "bad_packets": 0, "junk_bytes": 0, "unexpected_id": 0,
            "echo_detected": 0,
        }

    def transact_once(self, sid: int, instruction: int, params: bytes = b""):
        """送一包、收一包。回傳 (reply, rtt_ms)；逾時回傳 (None, None)。"""
        pkt = build_packet(sid, instruction, params)
        if pkt[4] not in ALLOWED_INSTRUCTIONS:          # 第二道檢查
            raise ReadOnlyViolation("送出前檢查失敗")

        self.port.reset_input_buffer()
        t0 = time.perf_counter()
        self.port.write(pkt)
        self.stats["packets_sent"] += 1
        deadline = t0 + self.timeout_s

        buf = bytearray()
        echo_checked = False
        while time.perf_counter() < deadline:
            n = self.port.in_waiting
            chunk = self.port.read(n if n else 1)
            if chunk:
                buf += chunk

            # echo 判斷（F-20 / V-06）：收到的開頭和剛送出的封包完全相同 → 是 echo
            if not echo_checked:
                if len(buf) >= len(pkt) or bytes(buf) != pkt[: len(buf)]:
                    if bytes(buf[: len(pkt)]) == pkt:
                        del buf[: len(pkt)]
                        self.stats["echo_detected"] += 1
                    echo_checked = True
                else:
                    continue   # 還分不出是 echo 還是回應，繼續收

            reply = try_parse(buf, self.stats)
            if reply is None:
                continue
            if reply["id"] != sid:
                self.stats["unexpected_id"] += 1
                continue
            rtt_ms = (time.perf_counter() - t0) * 1000.0
            return reply, rtt_ms

        self.stats["timeouts"] += 1
        return None, None

    def transact(self, sid: int, instruction: int, params: bytes = b""):
        for _ in range(self.retries):
            reply, rtt = self.transact_once(sid, instruction, params)
            if reply is not None:
                return reply, rtt
        return None, None

    def ping(self, sid: int):
        return self.transact(sid, INST_PING)

    def read(self, sid: int, addr: int, length: int):
        reply, _ = self.transact(sid, INST_READ, bytes([addr, length]))
        if reply is None or len(reply["params"]) != length:
            return None, reply
        return reply["params"], reply


# ---------------------------------------------------------------------------
# 快照與量測
# ---------------------------------------------------------------------------

def read_table(bus: Bus, sid: int) -> tuple[dict[int, int | None], list[str]]:
    """讀所有 READ_RANGES，回傳 {位址: byte 或 None} 與備註。"""
    table: dict[int, int | None] = {}
    notes: list[str] = []
    for start, length in READ_RANGES:
        data, reply = bus.read(sid, start, length)
        if data is None:
            notes.append(f"0x{start:02X}+{length} 整段讀取失敗，改為每 {FALLBACK_CHUNK} bytes 重讀")
            for off in range(0, length, FALLBACK_CHUNK):
                n = min(FALLBACK_CHUNK, length - off)
                part, _ = bus.read(sid, start + off, n)
                if part is None:
                    notes.append(f"0x{start + off:02X}+{n} 讀取失敗")
                for i in range(n):
                    table[start + off + i] = None if part is None else part[i]
            continue
        if reply and reply["error"]:
            notes.append(f"0x{start:02X}+{length} 回應 error byte = 0x{reply['error']:02X}")
        for i, b in enumerate(data):
            table[start + i] = b
    return table, notes


def decode(table: dict[int, int | None]) -> dict[str, int | None]:
    out: dict[str, int | None] = {}
    for addr, width, name, sign_bit in REGISTERS:
        raw_bytes = [table.get(addr + i) for i in range(width)]
        if any(b is None for b in raw_bytes):
            out[name] = None
            continue
        value = raw_bytes[0] if width == 1 else raw_bytes[0] | (raw_bytes[1] << 8)  # little-endian
        if sign_bit is not None and value & (1 << sign_bit):                       # sign-magnitude
            value = -(value & ((1 << sign_bit) - 1))
        out[name] = value
    return out


def raw_hex(table: dict[int, int | None]) -> dict[str, str]:
    """每段一行 hex，讀不到的 byte 以 '..' 表示。之後 diff 兩份快照最方便。"""
    out = {}
    for start, length in READ_RANGES:
        cells = []
        for a in range(start, start + length):
            b = table.get(a)
            cells.append(".." if b is None else f"{b:02X}")
        out[f"0x{start:02X}"] = " ".join(cells)
    return out


def measure_rtt(bus: Bus, sid: int, n: int) -> dict:
    """READ Present_Position（0x38，2 bytes）n 次，不重試，逾時另計。"""
    samples: list[float] = []
    timeouts = 0
    for _ in range(n):
        reply, rtt = bus.transact_once(sid, INST_READ, bytes([0x38, 2]))
        if reply is None:
            timeouts += 1
        else:
            samples.append(rtt)
    result = {"n": n, "ok": len(samples), "timeouts": timeouts}
    if samples:
        s = sorted(samples)
        result.update({
            "min_ms": round(s[0], 3),
            "median_ms": round(statistics.median(s), 3),
            "p95_ms": round(s[min(len(s) - 1, int(0.95 * len(s)))], 3),
            "max_ms": round(s[-1], 3),
            "mean_ms": round(statistics.fmean(s), 3),
        })
    return result


def scan(bus: Bus, id_range=range(0, 254)) -> list[int]:
    found = []
    old = bus.retries
    bus.retries = 1
    try:
        for sid in id_range:
            reply, _ = bus.ping(sid)
            if reply is not None:
                found.append(sid)
    finally:
        bus.retries = old
    return found


# ---------------------------------------------------------------------------
# 介面
# ---------------------------------------------------------------------------

def parse_ids(text: str) -> list[int]:
    ids: list[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            ids.extend(range(int(a), int(b) + 1))
        else:
            ids.append(int(part))
    for i in ids:
        if not 0 <= i <= 253:
            raise argparse.ArgumentTypeError(f"ID {i} 不合法")
    return ids


def open_port(spec: str, baud: int):
    if spec.startswith("fake"):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import fake_bus
        return fake_bus.from_spec(spec)
    import serial  # pyserial；lerobot 環境已內含
    return serial.Serial(spec, baudrate=baud, timeout=0.001, write_timeout=0.1)


def git_commit() -> str | None:
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=Path(__file__).resolve().parent,
            capture_output=True, text=True, timeout=3)
        return out.stdout.strip() or None
    except Exception:
        return None


def print_summary(motors: list[dict]) -> None:
    head = (f"{'ID':>3} {'model':>5} {'fw':>5} {'0x07':>4} {'0x12':>4} {'Vmax':>5} {'Vmin':>5} "
            f"{'Tmax':>5} {'min':>5} {'max':>5} {'ofs':>5} {'pos':>5} {'V':>5} {'°C':>3} "
            f"{'stat':>4} {'rtt50':>6} {'rtt95':>6}")
    print(head)
    print("-" * len(head))
    for m in motors:
        if not m["responded"]:
            print(f"{m['id']:>3}  （無回應）")
            continue
        r = m["registers"]

        def g(k, scale=None):
            v = r.get(k)
            if v is None:
                return "?"
            return f"{v / scale:.1f}" if scale else str(v)

        fw = "?" if r.get("Firmware_Major") is None else f"{r['Firmware_Major']}.{r['Firmware_Minor']}"
        rtt = m["rtt"]
        print(f"{m['id']:>3} {g('Model_Number'):>5} {fw:>5} {g('Return_Delay_Time'):>4} "
              f"{g('Phase'):>4} {g('Max_Voltage_Limit', 10):>5} {g('Min_Voltage_Limit', 10):>5} "
              f"{g('Max_Torque_Limit'):>5} {g('Min_Position_Limit'):>5} {g('Max_Position_Limit'):>5} "
              f"{g('Homing_Offset'):>5} {g('Present_Position'):>5} {g('Present_Voltage', 10):>5} "
              f"{g('Present_Temperature'):>3} {g('Status'):>4} "
              f"{rtt.get('median_ms', '?'):>6} {rtt.get('p95_ms', '?'):>6}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="STS3215 唯讀暫存器快照（只送 PING / READ）")
    ap.add_argument("--port", required=True, help="COM5 之類；或 fake / fake:echo,missing=3")
    ap.add_argument("--label", required=True, help="檔名標籤，例如 follower、leader、L3-pre")
    ap.add_argument("--baud", type=int, default=1_000_000)
    ap.add_argument("--ids", type=parse_ids, default=parse_ids("1-6"), help="預設 1-6；單顆用 --ids 1")
    ap.add_argument("--scan", action="store_true", help="先 PING 0–253 找出所有 ID（約數秒）")
    ap.add_argument("--rtt", type=int, default=100, help="每顆 RTT 量測次數，0 = 不量")
    ap.add_argument("--timeout-ms", type=float, default=50.0)
    ap.add_argument("--note", default="", help="自由備註，例如『組裝前、桌上串接、5V 電源』")
    ap.add_argument("--out", default=None, help="輸出資料夾，預設 repo 的 snapshots/")
    args = ap.parse_args(argv)

    port = open_port(args.port, args.baud)
    bus = Bus(port, timeout_s=args.timeout_ms / 1000.0)
    started = _dt.datetime.now().astimezone()

    try:
        if args.scan:
            print("掃描 ID 0–253 ...")
            ids = scan(bus)
            print(f"找到：{ids if ids else '（沒有）'}")
        else:
            ids = args.ids

        motors = []
        for sid in ids:
            reply, ping_rtt = bus.ping(sid)
            m = {"id": sid, "responded": reply is not None}
            if reply is None:
                motors.append(m)
                continue
            m["ping_error_byte"] = reply["error"]
            table, notes = read_table(bus, sid)
            m["raw"] = raw_hex(table)
            m["registers"] = decode(table)
            m["notes"] = notes
            m["rtt"] = measure_rtt(bus, sid, args.rtt) if args.rtt > 0 else {}
            motors.append(m)
    finally:
        port.close()

    result = {
        "meta": {
            "label": args.label,
            "started": started.isoformat(timespec="seconds"),
            "port": args.port,
            "baud": args.baud,
            "requested_ids": ids,
            "note": args.note,
            "script_version": SCRIPT_VERSION,
            "git_commit": git_commit(),
            "python": sys.version.split()[0],
            "platform": platform.platform(),
            "timeout_ms": args.timeout_ms,
            "rtt_method": "READ 0x38 len 2，time.perf_counter()，從 write 前到回應包解析完成；不重試",
        },
        "bus_stats": bus.stats,
        "motors": motors,
    }

    out_dir = Path(args.out) if args.out else Path(__file__).resolve().parent.parent / "snapshots"
    out_dir.mkdir(parents=True, exist_ok=True)
    safe_label = "".join(c if c.isalnum() or c in "-_" else "_" for c in args.label)
    path = out_dir / f"{started:%Y%m%d_%H%M%S}_{safe_label}.json"
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")

    print()
    print_summary(motors)
    print()
    s = bus.stats
    print(f"echo：{'有（轉接板會回送，V-06）' if s['echo_detected'] else '無'}　"
          f"逾時 {s['timeouts']}　checksum 錯 {s['bad_checksum']}　雜訊 {s['junk_bytes']} bytes")
    print(f"已存檔：{path}")

    missing = [m["id"] for m in motors if not m["responded"]]
    if missing:
        print(f"⚠️  無回應的 ID：{missing}（檢查電源、接線、ID 是否已設定）")
        return 2
    if not motors:
        print("⚠️  沒有任何馬達")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
