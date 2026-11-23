#!/usr/bin/env python3
"""驗算《通訊協議手冊》磁編碼版的全部範例封包。

對每一包檢查兩件事：
  1. checksum = ~(ID + Length + Instruction/Error + Params) & 0xFF
  2. Length   = 封包總長 - 4（字頭 2 + ID 1 + Length 1）

F-02 的證據來源：手冊 1.3.9 的 RESET 範例 checksum 印成 F6，正確為 F2。
其餘 14 包全部正確。

用法：
    python3 tools/verify.py          # 全部驗算，有錯則 exit 1
    python3 tools/verify.py -v       # 連正確的也逐包列出
"""

import sys

# (名稱, 手冊章節, 封包 hex)
PACKETS = [
    ("PING 指令",            "1.3.1", "FF FF 01 02 01 FB"),
    ("PING 回應",            "1.3.1", "FF FF 01 02 00 FC"),
    ("READ 指令",            "1.3.2", "FF FF 01 04 02 38 02 BE"),
    ("READ 回應",            "1.3.2", "FF FF 01 04 00 18 05 DD"),
    ("WRITE 設定 ID",         "1.3.3", "FF FF FE 04 03 05 01 F4"),
    ("WRITE 位置+速度",       "1.3.3", "FF FF 01 09 03 2A 00 08 00 00 E8 03 D5"),
    ("REG WRITE ID1",        "1.3.4", "FF FF 01 09 04 2A 00 08 00 00 E8 03 D4"),
    ("REG WRITE ID10",       "1.3.4", "FF FF 0A 09 04 2A 00 08 00 00 E8 03 CB"),
    ("ACTION",               "1.3.5", "FF FF FE 02 05 FA"),
    ("SYNC WRITE 四軸",       "1.3.6", "FF FF FE 20 83 2A 06"
                                      " 01 00 08 00 00 E8 03"
                                      " 02 00 08 00 00 E8 03"
                                      " 03 00 08 00 00 E8 03"
                                      " 04 00 08 00 00 E8 03 58"),
    ("SYNC READ 指令",        "1.3.7", "FF FF FE 06 82 38 08 01 02 36"),
    ("SYNC READ 回應 ID1",    "1.3.7", "FF FF 01 0A 00 00 08 00 00 00 00 79 1E 55"),
    ("SYNC READ 回應 ID2",    "1.3.7", "FF FF 02 0A 00 FF 07 00 00 00 00 77 23 53"),
    ("RECOVERY",             "1.3.8", "FF FF 01 02 06 F6"),
    ("RESET",                "1.3.9", "FF FF 01 02 0A F6"),
]


def checksum(body: list[int]) -> int:
    """body 為 ID 起算、不含 checksum 的全部 byte。"""
    return (~sum(body)) & 0xFF


def check(hex_str: str) -> tuple[bool, int, int, bool, int, int]:
    pkt = [int(b, 16) for b in hex_str.split()]
    body, got_cks = pkt[2:-1], pkt[-1]
    want_cks = checksum(body)
    got_len, want_len = pkt[3], len(pkt) - 4
    return (got_cks == want_cks, got_cks, want_cks,
            got_len == want_len, got_len, want_len)


def main(verbose: bool) -> int:
    bad = []
    for name, sec, hex_str in PACKETS:
        ok_c, got_c, want_c, ok_l, got_l, want_l = check(hex_str)
        if ok_c and ok_l:
            if verbose:
                print(f"  OK   {sec:<6} {name}")
            continue
        bad.append(name)
        print(f"  FAIL {sec:<6} {name}")
        if not ok_c:
            print(f"         checksum 手冊印 {got_c:02X}，驗算為 {want_c:02X}")
        if not ok_l:
            print(f"         Length   手冊印 {got_l:02X}，應為 {want_l:02X}")

    total = len(PACKETS)
    print(f"\n{total} 包中 {total - len(bad)} 包正確，{len(bad)} 包有誤。")
    if bad:
        print("有誤者：" + "、".join(bad))
        print("\n已知：RESET 為手冊印刷錯誤（F-02），非本工具的問題。")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main("-v" in sys.argv))
