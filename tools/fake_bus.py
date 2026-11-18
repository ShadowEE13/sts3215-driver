#!/usr/bin/env python3
"""
fake_bus.py — 在家測 snapshot.py 用的假匯流排（行程內，不需要虛擬序列埠）

模擬幾顆 STS3215 接在同一條半雙工匯流排上，提供 snapshot.py 用到的 pyserial
介面子集：write()、read()、in_waiting、reset_input_buffer()、close()。

出廠值與 sim/servo_sim.c 的 factory_eprom() / power_on_sram() 一致，
兩邊的「原廠值」只有一個來源：memory-table-check.md 第四節。

故障注入（用 --port 帶參數，逗號分隔）
    fake                     6 顆，ID 1–6
    fake:ids=1               只有 1 顆（模擬尚未設 ID 的出廠馬達）
    fake:echo                轉接板會回送（F-20 / V-06）
    fake:missing=3           ID 3 不回應（接線鬆脫）
    fake:corrupt=0.2         20% 回應包 checksum 錯誤
    fake:noise               回應前插入雜訊 bytes
    fake:limit=0x47          0x47 之後讀不到（不回應），測 fallback

額外的防線：收到 PING / READ 以外的指令時丟 WriteAttempted。
就算 snapshot.py 的唯讀檢查哪天被改壞，用 fake 測試時也會立刻炸掉。

直接執行本檔 = 跑 snapshot.py 的自我測試：
    python tools/fake_bus.py
"""

from __future__ import annotations

import random

TABLE_SIZE = 128
EPROM_END = 0x28


class WriteAttempted(Exception):
    """假匯流排收到會改寫暫存器的指令。"""


def _put16(t: bytearray, addr: int, v: int) -> None:
    t[addr] = v & 0xFF
    t[addr + 1] = (v >> 8) & 0xFF


def factory_table(sid: int, present: int = 2048) -> bytearray:
    t = bytearray(TABLE_SIZE)
    # --- EPROM（同 servo_sim.c factory_eprom）---
    _put16(t, 0x03, 777)   # 型號
    t[0x05] = sid
    t[0x06] = 0            # 1 Mbps
    t[0x07] = 250          # 500 µs
    t[0x08] = 1
    _put16(t, 0x09, 0)
    _put16(t, 0x0B, 4095)
    t[0x0D] = 70
    _put16(t, 0x10, 1000)
    t[0x12] = 0            # 原廠表沒給，模擬器假設 0
    t[0x13] = 44
    t[0x14] = 47
    t[0x1A] = 1
    t[0x1B] = 1
    t[0x1E] = 1
    t[0x22] = 20
    t[0x23] = 200
    t[0x24] = 80
    t[0x26] = 200
    # --- SRAM（同 power_on_sram）---
    _put16(t, 0x30, 1000)  # 由 0x10 複製
    t[0x37] = 0
    _put16(t, 0x38, present)
    _put16(t, 0x2A, present)
    t[0x3E] = 120
    t[0x3F] = 25
    return t


def _checksum(body: bytes) -> int:
    return (~sum(body)) & 0xFF


class FakeBus:
    def __init__(self, ids=(1, 2, 3, 4, 5, 6), echo=False, missing=(), corrupt=0.0,
                 noise=False, limit=TABLE_SIZE, seed=0):
        self.tables = {sid: factory_table(sid, 2000 + 10 * sid) for sid in ids}
        self.echo = echo
        self.missing = set(missing)
        self.corrupt = corrupt
        self.noise = noise
        self.limit = limit
        self.rng = random.Random(seed)
        self.timeout = 0.001
        self._rx = bytearray()   # 送到馬達的
        self._tx = bytearray()   # 馬達回來的（給上位機讀）
        self.instructions_seen: list[int] = []

    # ---- pyserial 介面子集 ----
    @property
    def in_waiting(self) -> int:
        return len(self._tx)

    def read(self, n: int = 1) -> bytes:
        out = bytes(self._tx[:n])
        del self._tx[:n]
        return out

    def write(self, data: bytes) -> int:
        if self.echo:
            self._tx += data
        self._rx += data
        self._process()
        return len(data)

    def reset_input_buffer(self) -> None:
        self._tx.clear()

    def close(self) -> None:
        pass

    # ---- 馬達端 ----
    def _process(self) -> None:
        while True:
            start = self._rx.find(b"\xff\xff")
            if start < 0 or len(self._rx) < start + 4:
                return
            del self._rx[:start]
            total = 4 + self._rx[3]
            if len(self._rx) < total:
                return
            pkt = bytes(self._rx[:total])
            del self._rx[:total]
            if _checksum(pkt[2:-1]) != pkt[-1]:
                continue                      # 真馬達也是靜靜丟掉
            sid, inst, params = pkt[2], pkt[4], pkt[5:-1]
            self.instructions_seen.append(inst)
            if inst not in (0x01, 0x02):
                raise WriteAttempted(f"收到 instruction 0x{inst:02X}（ID {sid}）")
            if sid not in self.tables or sid in self.missing:
                continue
            if inst == 0x01:
                self._reply(sid, b"")
            else:
                addr, length = params[0], params[1]
                if addr + length > self.limit:
                    continue                  # 模擬讀不到的區段
                self._reply(sid, bytes(self.tables[sid][addr: addr + length]))

    def _reply(self, sid: int, params: bytes) -> None:
        body = bytes([sid, len(params) + 2, 0x00]) + params
        chk = _checksum(body)
        if self.corrupt and self.rng.random() < self.corrupt:
            chk ^= 0x5A
        if self.noise:
            self._tx += bytes([0x00, 0xFF, 0x13])
        self._tx += b"\xff\xff" + body + bytes([chk])


def from_spec(spec: str) -> FakeBus:
    """'fake:echo,missing=3,ids=1-6' → FakeBus"""
    kw: dict = {}
    opts = spec.split(":", 1)[1] if ":" in spec else ""
    for opt in filter(None, (o.strip() for o in opts.split(","))):
        key, _, val = opt.partition("=")
        if key == "echo":
            kw["echo"] = True
        elif key == "noise":
            kw["noise"] = True
        elif key == "missing":
            kw["missing"] = [int(x) for x in val.split("+")]
        elif key == "ids":
            a, _, b = val.partition("-")
            kw["ids"] = tuple(range(int(a), int(b or a) + 1))
        elif key == "corrupt":
            kw["corrupt"] = float(val)
        elif key == "limit":
            kw["limit"] = int(val, 0)
        else:
            raise ValueError(f"fake_bus 不認得的選項：{opt}")
    return FakeBus(**kw)


# ---------------------------------------------------------------------------
# 自我測試：python tools/fake_bus.py
# ---------------------------------------------------------------------------

def _selftest() -> int:
    import json
    import sys
    import tempfile
    from pathlib import Path

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import snapshot as S

    passed = failed = 0

    def check(name, cond):
        nonlocal passed, failed
        if cond:
            passed += 1
        else:
            failed += 1
            print(f"  ✗ {name}")

    def snap(spec, ids=range(1, 7), rtt=5):
        bus = S.Bus(from_spec(spec), timeout_s=0.01)
        res = {}
        for sid in ids:
            reply, _ = bus.ping(sid)
            if reply is None:
                res[sid] = None
                continue
            table, notes = S.read_table(bus, sid)
            res[sid] = {"reg": S.decode(table), "raw": S.raw_hex(table), "notes": notes,
                        "rtt": S.measure_rtt(bus, sid, rtt)}
        return bus, res

    print("1. 組包：手冊範例")
    # 手冊 READ 範例：讀 ID 1 的 0x38 兩個 byte → FF FF 01 04 02 38 02 BE
    check("READ 範例", S.build_packet(1, 0x02, bytes([0x38, 2])) == bytes.fromhex("FFFF0104023802BE"))
    check("PING 範例", S.build_packet(1, 0x01) == bytes.fromhex("FFFF010201FB"))

    print("2. 唯讀保證")
    for inst in (0x03, 0x04, 0x05, 0x06, 0x82, 0x83):
        try:
            S.build_packet(1, inst, b"\x00")
            check(f"0x{inst:02X} 應被拒絕", False)
        except S.ReadOnlyViolation:
            check(f"0x{inst:02X} 被拒絕", True)
    try:
        S.build_packet(0xFE, 0x01)
        check("廣播 ID 應被拒絕", False)
    except ValueError:
        check("廣播 ID 被拒絕", True)
    fb = FakeBus()
    try:
        # 繞過 build_packet，直接把一個合法的 WRITE（ID 1 的 0x05 寫成 2）丟進匯流排
        fb.write(b"\xff\xff\x01\x04\x03\x05\x02" + bytes([(~(1 + 4 + 3 + 5 + 2)) & 0xFF]))
        check("fake_bus 應攔下 WRITE", False)
    except WriteAttempted:
        check("fake_bus 攔下 WRITE", True)

    print("3. 正常 6 顆")
    bus, r = snap("fake")
    check("6 顆都回應", all(r[i] is not None for i in range(1, 7)))
    check("型號 777", all(r[i]["reg"]["Model_Number"] == 777 for i in range(1, 7)))
    check("0x07 = 250", r[1]["reg"]["Return_Delay_Time"] == 250)
    check("0x0D = 70", r[1]["reg"]["Max_Temperature_Limit"] == 70)
    check("0x13 = 44", r[1]["reg"]["Unloading_Condition"] == 44)
    check("ID 欄位正確", all(r[i]["reg"]["ID"] == i for i in range(1, 7)))
    check("Present_Position 各不同", r[3]["reg"]["Present_Position"] == 2030)
    check("無 echo", bus.stats["echo_detected"] == 0)
    check("RTT 全部成功", r[1]["rtt"]["ok"] == 5)
    check("0x47 段有讀到", ".." not in r[1]["raw"]["0x47"])

    print("4. 轉接板 echo（V-06）")
    bus, r2 = snap("fake:echo")
    check("偵測到 echo", bus.stats["echo_detected"] > 0)
    check("echo 下資料與正常相同", r2[2]["raw"] == r[2]["raw"])
    check("echo 未被當成回應", bus.stats["timeouts"] == 0)

    print("5. 單顆出廠馬達（ids=1）")
    bus, r3 = snap("fake:ids=1", ids=[1])
    check("ID 1 回應", r3[1] is not None)
    check("掃描只找到 [1]", S.scan(S.Bus(from_spec("fake:ids=1"), timeout_s=0.002), range(0, 10)) == [1])

    print("6. 缺一顆")
    bus, r4 = snap("fake:missing=3")
    check("ID 3 無回應", r4[3] is None)
    check("其他照常", r4[4] is not None)

    print("7. checksum 錯誤 20% + 雜訊")
    bus, r5 = snap("fake:corrupt=0.2,noise", rtt=50)
    check("重試後表格完整", all(".." not in v for v in r5[1]["raw"].values()))
    check("有計到 checksum 錯", bus.stats["bad_checksum"] > 0)
    check("有計到雜訊", bus.stats["junk_bytes"] > 0)
    check("資料與正常相同", r5[1]["raw"] == r[1]["raw"])

    print("8. 0x47 之後讀不到 → fallback")
    bus, r6 = snap("fake:limit=0x47", rtt=0)
    check("0x47 段標成 ..", set(r6[1]["raw"]["0x47"].split()) == {".."})
    check("前兩段完整", ".." not in r6[1]["raw"]["0x00"] + r6[1]["raw"]["0x28"])
    check("有留備註", any("失敗" in n for n in r6[1]["notes"]))

    print("9. sign-magnitude 解碼")
    t = {0x1F: 0x64, 0x20: 0x08}          # 0x0864，BIT11 = 1 → -100
    check("Homing_Offset -100", S.decode(t)["Homing_Offset"] == -100)

    print("10. 端到端：main() 寫出 JSON")
    with tempfile.TemporaryDirectory() as d:
        code = S.main(["--port", "fake:echo", "--label", "selftest", "--rtt", "3", "--out", d])
        files = list(Path(d).glob("*_selftest.json"))
        check("回傳 0", code == 0)
        check("寫出一個檔", len(files) == 1)
        if files:
            data = json.loads(files[0].read_text(encoding="utf-8"))
            check("6 顆", len(data["motors"]) == 6)
            check("meta 有 label", data["meta"]["label"] == "selftest")
            check("echo 有記錄", data["bus_stats"]["echo_detected"] > 0)
            pk = data["motors"][0]["packets"]
            check("參考封包：第一包是 PING", pk[0]["tx"] == "ff ff 01 02 01 fb")
            check("參考封包：PING 回應", pk[0]["rx"] == "ff ff 01 02 00 fc")
            check("參考封包：標記 echo", pk[0]["echo"] is True)
            check("參考封包：PING + 三段 READ", len(pk) == 4)
            check("參考封包：READ 0x00 長度 40", pk[1]["tx"] == "ff ff 01 04 02 00 28 d0")
        code = S.main(["--port", "fake:missing=2", "--label", "m", "--rtt", "0", "--out", d])
        check("缺馬達時回傳 2", code == 2)
        f2 = [f for f in Path(d).glob("*_m.json")][0]
        miss = json.loads(f2.read_text(encoding="utf-8"))["motors"][1]
        check("缺馬達：記錄三次 PING 皆無回應", len(miss["packets"]) == 3 and all(p["rx"] is None for p in miss["packets"]))

    print()
    print(f"通過 {passed}，失敗 {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(_selftest())
