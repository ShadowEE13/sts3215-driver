"""對照 cadence.py 對 LeRobot 的七個假設，不連硬體。

用途：進實驗室前先確認 tools/cadence.py 的耦合段還對得上本機的 LeRobot。
LeRobot 改版頻繁（F-30：後端已從 scservo_sdk 換成 rustypot），模組路徑與類別名稱
都可能變。這支只做 import 與 introspection，不開序列埠、不連馬達，在家就能跑。

用法（Miniforge Prompt）：
    conda activate lerobot
    python tools/probe_lerobot.py

猜測的路徑找不到時會自動搜尋整個 lerobot 套件，把真正的位置印出來。
"""

from __future__ import annotations

import dataclasses
import importlib
import inspect
import pkgutil
import sys

OK, BAD, WARN = "[ OK ]", "[FAIL]", "[warn]"

# cadence.py 目前假設的 (模組路徑, 類別名稱)
GUESSES = [
    ("lerobot.robots.so101_follower", "SO101Follower"),
    ("lerobot.robots.so101_follower", "SO101FollowerConfig"),
    ("lerobot.teleoperators.so101_leader", "SO101Leader"),
    ("lerobot.teleoperators.so101_leader", "SO101LeaderConfig"),
]

# cadence.py 會呼叫的方法
FOLLOWER_METHODS = ["connect", "disconnect", "get_observation", "send_action"]
LEADER_METHODS = ["connect", "disconnect", "get_action"]

problems: list[str] = []


def head(title: str) -> None:
    print(f"\n{'─' * 68}\n{title}\n{'─' * 68}")


def find_class(name: str) -> list[str]:
    """猜測失敗時，掃過整個 lerobot 套件找這個類別在哪。"""
    import lerobot

    found = []
    for mod in pkgutil.walk_packages(lerobot.__path__, "lerobot."):
        # 只看 robots / teleoperators 底下的模組，避免 import 一堆不相干的重東西。
        #
        # 2026-11-28 修正：原本的條件是「模組名含 so101」，結果 0.6.2 把模組改名為
        # so_follower / so_leader（類別名才留著 SO101），整個搜尋直接跳過了正確答案，
        # 回報「整個 lerobot 裡都找不到」——比不回報更糟，因為它看起來像個結論。
        # 搜尋的篩選條件必須比它要找的東西寬，否則找不到的時候分不出是真的沒有，
        # 還是篩掉了。
        if not mod.name.startswith(("lerobot.robots.", "lerobot.teleoperators.")):
            continue
        try:
            m = importlib.import_module(mod.name)
        except Exception:
            continue
        if hasattr(m, name):
            found.append(mod.name)
    return found


# ───────────────────────── 0. 版本 ─────────────────────────
head("0. 環境")
print(f"Python          {sys.version.split()[0]}")
try:
    import lerobot

    print(f"lerobot         {getattr(lerobot, '__version__', '(無 __version__)')}")
    print(f"安裝位置        {lerobot.__path__[0]}")
except Exception as exc:
    print(f"{BAD} 連 lerobot 都 import 不了：{exc}")
    print("      確認是不是忘了 conda activate lerobot")
    sys.exit(1)

for extra in ("rustypot", "scservo_sdk"):
    try:
        m = importlib.import_module(extra)
        print(f"{extra:<15} 有，{getattr(m, '__version__', '(無版本字串)')}")
    except Exception:
        print(f"{extra:<15} 沒有")

# ───────────────────── 1-2. import 路徑 ─────────────────────
head("1-2. cadence.py 的 import 路徑")
resolved: dict[str, type] = {}
for mod_path, cls_name in GUESSES:
    try:
        mod = importlib.import_module(mod_path)
        cls = getattr(mod, cls_name)
        resolved[cls_name] = cls
        print(f"{OK} from {mod_path} import {cls_name}")
    except Exception as exc:
        print(f"{BAD} from {mod_path} import {cls_name}")
        print(f"       {type(exc).__name__}: {exc}")
        hits = find_class(cls_name)
        if hits:
            print(f"       → 實際上在：{', '.join(hits)}")
            problems.append(f"{cls_name} 應改為 {hits[0]}")
            try:
                resolved[cls_name] = getattr(importlib.import_module(hits[0]), cls_name)
            except Exception:
                pass
        else:
            print("       → 整個 lerobot 裡都找不到這個名字（類別被改名了？）")
            problems.append(f"{cls_name} 不存在")

# ───────────────────── 3. Config 建構參數 ─────────────────────
head("3. Config 的建構參數（cadence.py 只傳 port= 與 id=）")
for cfg_name in ("SO101FollowerConfig", "SO101LeaderConfig"):
    cls = resolved.get(cfg_name)
    if cls is None:
        print(f"{BAD} {cfg_name} 前面就沒找到，跳過")
        continue
    if dataclasses.is_dataclass(cls):
        names = []
        required = []
        for f in dataclasses.fields(cls):
            has_default = (
                f.default is not dataclasses.MISSING
                or f.default_factory is not dataclasses.MISSING  # type: ignore[misc]
            )
            names.append(f.name)
            if not has_default:
                required.append(f.name)
        print(f"{OK} {cfg_name} 是 dataclass")
        print(f"       全部欄位：{', '.join(names)}")
        print(f"       沒有預設值（必填）：{', '.join(required) or '（無）'}")
        for need in ("port", "id"):
            if need not in names:
                print(f"{BAD}   沒有 {need!r} 這個欄位")
                problems.append(f"{cfg_name} 沒有 {need} 欄位")
        missing = [r for r in required if r not in ("port", "id")]
        if missing:
            print(f"{BAD}   還有必填欄位 cadence.py 沒傳：{', '.join(missing)}")
            problems.append(f"{cfg_name} 還需要 {', '.join(missing)}")
    else:
        try:
            print(f"{WARN} {cfg_name} 不是 dataclass，簽章：{inspect.signature(cls)}")
        except (TypeError, ValueError):
            print(f"{WARN} {cfg_name} 不是 dataclass，也讀不到簽章")
        problems.append(f"{cfg_name} 不是 dataclass，要人工看")

# ───────────────────── 4-5. 方法 ─────────────────────
head("4-5. cadence.py 呼叫的方法")
for cls_name, methods in (
    ("SO101Follower", FOLLOWER_METHODS),
    ("SO101Leader", LEADER_METHODS),
):
    cls = resolved.get(cls_name)
    if cls is None:
        print(f"{BAD} {cls_name} 前面就沒找到，跳過")
        continue
    print(f"\n{cls_name}")
    for meth in methods:
        fn = getattr(cls, meth, None)
        if fn is None:
            print(f"  {BAD} {meth}() 不存在")
            cands = [
                a
                for a in dir(cls)
                if not a.startswith("_") and meth.split("_")[-1] in a.lower()
            ]
            if cands:
                print(f"         名字相近的：{', '.join(cands)}")
            problems.append(f"{cls_name}.{meth}() 不存在")
        else:
            try:
                sig = str(inspect.signature(fn))
            except (TypeError, ValueError):
                sig = "(讀不到簽章)"
            print(f"  {OK} {meth}{sig}")

# ───────────────────── 6. 溫度讀取 ─────────────────────
head("6. 溫度：follower.bus.sync_read('Present_Temperature')")
cls = resolved.get("SO101Follower")
if cls is None:
    print(f"{BAD} SO101Follower 沒找到，跳過")
else:
    # bus 多半是 __init__ 裡建的實例屬性，類別上看不到，所以看原始碼
    try:
        src = inspect.getsource(cls)
    except OSError:
        src = ""
    if "self.bus" in src:
        print(f"{OK} __init__ 裡有 self.bus")
    else:
        print(f"{WARN} 類別原始碼裡沒看到 self.bus")
        attrs = [a for a in dir(cls) if "bus" in a.lower()]
        print(f"       名字含 bus 的屬性：{', '.join(attrs) or '（無）'}")
        problems.append("follower.bus 可能不叫這個名字")

    # 找 bus 類別，確認 sync_read 的簽章與溫度暫存器名稱
    try:
        from lerobot.motors.feetech import FeetechMotorsBus

        fn = getattr(FeetechMotorsBus, "sync_read", None)
        if fn:
            print(f"{OK} FeetechMotorsBus.sync_read{inspect.signature(fn)}")
        else:
            print(f"{BAD} FeetechMotorsBus 沒有 sync_read")
            problems.append("sync_read 不存在")
        from lerobot.motors.feetech import tables  # noqa: F401

        print(f"{OK} 找得到 feetech.tables（暫存器名稱表）")
    except Exception as exc:
        print(f"{WARN} 讀不到 FeetechMotorsBus / tables：{type(exc).__name__}: {exc}")
        print("       溫度那段只能等真機時看，但它已經包在 try 裡，失敗不會中斷實驗")

# ───────────────────── 7. 型別串接 ─────────────────────
head("7. get_action() 的回傳能不能直接餵給 send_action()")
lead = resolved.get("SO101Leader")
foll = resolved.get("SO101Follower")
if lead and foll:
    ga = getattr(lead, "get_action", None)
    sa = getattr(foll, "send_action", None)
    for label, fn in (("get_action 回傳", ga), ("send_action 參數", sa)):
        if fn is None:
            continue
        try:
            sig = inspect.signature(fn)
            ann = (
                sig.return_annotation
                if label.startswith("get")
                else [
                    f"{p.name}: {p.annotation}"
                    for p in sig.parameters.values()
                    if p.name != "self"
                ]
            )
            print(f"  {label}：{ann}")
        except (TypeError, ValueError):
            print(f"  {WARN} {label}：讀不到")
    print("\n  註：LeRobot 的 teleoperate 就是這樣串的，型別不合的機率低，")
    print("      但 leader 與 follower 的馬達名稱若不同，dict 的 key 會對不上。")
    print("      真機第一次跑時要比對兩邊的 key。")

# ───────────────────── 結論 ─────────────────────
head("結論")
if problems:
    print(f"{BAD} 有 {len(problems)} 處要改：\n")
    for i, p in enumerate(problems, 1):
        print(f"  {i}. {p}")
    print("\n把上面整份輸出貼回對話，我改 cadence.py。")
    sys.exit(1)
else:
    print(f"{OK} cadence.py 的七個假設全部對得上，可以直接進實驗室。")
    print("     唯一沒驗到的是真機行為（port 名稱、馬達 key、實際溫度讀取），")
    print("     那些要接上手臂才知道。")
