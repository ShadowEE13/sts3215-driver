# 上層控制介面規格

**版本**：v1.2
**日期**：2026/11/05
**負責**：C（控制與通訊層）
**對象**：B（橋接程式）、下學期的 A（手臂狀態回傳）

這份文件定義 **L3：橋接 → 控制層** 的 UDP 封包格式。B 依此實作橋接程式的發送端，C 實作接收端。

規格一旦凍結，後端從 LeRobot 換成自製 C 驅動時，對 B 完全無感。

---

## 1. 傳輸層

| 項目 | 值 |
|---|---|
| 協定 | UDP over IPv4 |
| 位址 | `127.0.0.1`（localhost） |
| 指令埠（B → C） | **49200** |
| 狀態埠（C → B） | **49201**（選用，預設關閉） |
| 位元組序 | **little-endian**，全欄位一致 |
| 最大封包 | 512 bytes |

**為什麼用 UDP 而非 TCP**：控制資料會過期。掉了一包，要的是下一包的新位置，不是重傳三十毫秒前的舊位置。TCP 的 head-of-line blocking 會讓一次丟包卡住後續所有已到達的資料，對控制流是錯的行為。

---

## 2. 封包結構

### 2.1 標頭（20 bytes，固定）

| 位移 | 大小 | 欄位 | 型別 | 說明 |
|---:|---:|---|---|---|
| 0 | 2 | `magic` | uint16 | 固定 `0x5343`。線上 bytes 為 `43 53`，見下方說明 |
| 2 | 1 | `version` | uint8 | 協定版本，目前 `0x01` |
| 3 | 1 | `cmd_type` | uint8 | 指令類型，見 §3 |
| 4 | 4 | `seq` | uint32 | 遞增序號，由發送端產生 |
| 8 | 8 | `timestamp_us` | uint64 | 發送端時間戳，微秒。時鐘定義見 §2.4 |
| 16 | 1 | `payload_len` | uint8 | payload 位元組數，可為 0 |
| 17 | 3 | `reserved` | uint8[3] | 填 0。保留供日後擴充 |

標頭長度定為 20 bytes，使 payload 起點 4-byte 對齊，接收端可直接以結構指標讀取而不需逐欄搬移。

**`magic` 一律以 uint16 比對，不要當字串比對。** 因為全欄位 little-endian，`0x5343` 在線上的 bytes 是 `43 53`，以 ASCII 讀是 `"CS"` 而不是 `"SC"`。發送端寫 `struct.pack("<H", 0x5343)`，接收端比對 `magic == 0x5343`；若發送端直接寫 `b"SC"`，接收端解出的是 `0x4353`，會被判為 `BAD_VERSION`。

### 2.2 Payload

長度 = `payload_len`，內容依 `cmd_type` 而定，見 §4。

### 2.3 CRC（2 bytes）

緊接在 payload 之後。

| 項目 | 值 |
|---|---|
| 演算法 | CRC-16/CCITT-FALSE |
| 多項式 | `0x1021` |
| 初始值 | `0xFFFF` |
| 輸入反射 | 否 |
| 輸出反射 | 否 |
| 最終 XOR | `0x0000` |
| 涵蓋範圍 | 位移 0 到 `19 + payload_len`（標頭 + payload，不含 CRC 自身） |

**為什麼 localhost 還要 CRC**：UDP 本身有檢查碼，但作業系統在回送介面上可能略過驗證。CRC 在這裡的真正用途不是防傳輸錯誤，是在整合階段擋下**兩端格式不一致**——B 的結構多一個 byte、少一次對齊，CRC 立刻失敗，不會變成手臂亂動才發現。日後若改走無線，這個欄位原封不動就能用。

參考實作：

```c
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}
```

**驗證用資料**：兩端各自實作後，先用下面兩組資料對照，結果一致再開始整合。

- CRC 檢查值：ASCII `"123456789"`（9 bytes）→ `0x29B1`。這是 CRC-16/CCITT-FALSE 的標準檢查值
- 完整封包：`NOP`、`seq = 1`、`timestamp_us = 1730000000000000`，共 22 bytes：

```
43 53 01 00 01 00 00 00 00 20 76 03 6D 25 06 00 00 00 00 00 50 A4
```

| 欄位 | bytes |
|---|---|
| `magic` | `43 53` |
| `version` | `01` |
| `cmd_type` | `00` |
| `seq` | `01 00 00 00` |
| `timestamp_us` | `00 20 76 03 6D 25 06 00` |
| `payload_len` | `00` |
| `reserved` | `00 00 00` |
| CRC | `50 A4`（即 `0xA450`） |

### 2.4 `timestamp_us` 的時鐘定義

**規定：Unix epoch（1970-01-01 UTC）起算的微秒數，取牆鐘時間。**

兩端必須使用同一個 epoch，否則相減得不到延遲，只會得到兩個時鐘原點的差。本欄位是 W12 延遲量測的唯一依據，定義錯了量不出任何東西，因此在此明確規定，不留給實作決定。

| | 用什麼 | 不要用 |
|---|---|---|
| B（Python 橋接） | `time.time_ns() // 1000`，Windows 上需 Python ≥ 3.13（見下） | `time.perf_counter()`、`time.monotonic()` |
| C 第一版（LeRobot，Python） | 同 B | 同 B |
| C 第二版（自製 C 驅動，Windows） | `GetSystemTimePreciseAsFileTime()` 後換算 | `QueryPerformanceCounter()`、`GetSystemTimeAsFileTime()` |

**為什麼不能用 `perf_counter` 或 QPC**：這兩者的起點是行程啟動或系統開機後的任意時刻，不是共同的 epoch。跨行程相減沒有意義。它們適合量測「同一個行程內兩個時間點的間隔」，不適合量測「兩個行程之間的傳遞時間」。

**Windows 上 Python 必須 ≥ 3.13**：Python 3.12 以前，`time.time()` 與 `time.time_ns()` 在 Windows 上底層都是 `GetSystemTimeAsFileTime()`，實際解析度約 15.6 ms（系統計時器節拍），而我們要量的迴路只有數毫秒——量測誤差會比被量的對象還大。兩個函式讀的是同一個時鐘，換函式沒有用。Python 3.13 起改用 `GetSystemTimePreciseAsFileTime()`，解析度優於 1 µs。

確認方式（`implementation` 必須是 `GetSystemTimePreciseAsFileTime()`）：

```
python -c "import time; print(time.get_clock_info('time'))"
```

舊版實測有時會比 15.6 ms 細，那取決於其他程式有沒有調整系統計時器，不可依賴。

若環境卡在 3.12 以前（例如某個套件尚未支援 3.13），用 ctypes 直接呼叫同一個 API：

```python
import ctypes, sys, time

if sys.platform == "win32" and sys.version_info < (3, 13):
    _ft = ctypes.c_uint64()  # FILETIME 為 low、high 兩個 DWORD，little-endian 下等同一個 uint64
    _get = ctypes.windll.kernel32.GetSystemTimePreciseAsFileTime

    def now_us() -> int:
        _get(ctypes.byref(_ft))
        return (_ft.value - 116444736000000000) // 10
else:
    def now_us() -> int:
        return time.time_ns() // 1000
```

**為什麼用 `time_ns()` 而不是 `time.time()`**：兩者時鐘相同、解析度相同。差別在 `time_ns()` 回傳整數，直接對應 uint64 欄位，不經過 float 換算。

**C 端換算**：`FILETIME` 以 100 ns 為單位、自 1601-01-01 起算。轉為本欄位的值：

```c
static uint64_t now_unix_us(void)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);          /* 100ns 單位，1601 epoch */
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ULL) / 10ULL;  /* → µs，1970 epoch */
}
```

常數 `116444736000000000` 是 1601 到 1970 之間的 100 ns 單位數。

**本學期可量測的範圍**：B 橋接蓋章 → 控制層 → 手臂 → 回應包回到控制層。完整的端到端延遲（直到操作者在頭盔看到畫面）需要 L10 手臂狀態回傳與影像鏈，兩者都在下學期，本學期量不到。詳見 `system-comm.md` §3。

---

## 3. 指令類型

| 值 | 名稱 | payload | 本學期狀態 |
|---:|---|---:|---|
| `0x00` | `NOP` | 0 | 支援。心跳用，重置 watchdog 但不改變目標 |
| `0x01` | `JOINT` | 24 | **支援**。六軸關節角度目標 |
| `0x02` | `POSE` | 24 | 定義但不實作，回 `NOT_IMPLEMENTED` |
| `0x03` | `GRIPPER` | 4 | 支援。夾爪開合 |
| `0x10` | `ESTOP` | 0 | 支援。立即停止並鎖定 |
| `0x11` | `RESUME` | 0 | 支援。解除急停 |
| `0x20` | `STATE_REQ` | 0 | 下學期 |

`POSE` 本學期不實作，因為逆運動學排在下學期。但欄位定義現在就凍結，B 可以照 `POSE` 格式開發，下學期 IK 補上即可接通，介面不需再動。

---

## 4. Payload 格式

全部使用**整數定點數**，不使用浮點。理由：數值表示在兩端完全確定、沒有格式化歧異、與伺服機暫存器的整數世界一致，且日後若把橋接移到無 FPU 的 MCU 上也不需改動。

### 4.1 `JOINT`（0x01）— 24 bytes

| 位移 | 大小 | 欄位 | 型別 | 單位 |
|---:|---:|---|---|---|
| 0 | 4 | `j1` | int32 | 0.001 度 |
| 4 | 4 | `j2` | int32 | 0.001 度 |
| 8 | 4 | `j3` | int32 | 0.001 度 |
| 12 | 4 | `j4` | int32 | 0.001 度 |
| 16 | 4 | `j5` | int32 | 0.001 度 |
| 20 | 4 | `j6` | int32 | 0.001 度 |

關節編號 J1 為基座，往末端遞增。例：`j1 = 45000` 表示第一軸 45.000 度。

### 4.2 `POSE`（0x02）— 24 bytes

| 位移 | 大小 | 欄位 | 型別 | 單位 |
|---:|---:|---|---|---|
| 0 | 4 | `x` | int32 | 0.01 mm |
| 4 | 4 | `y` | int32 | 0.01 mm |
| 8 | 4 | `z` | int32 | 0.01 mm |
| 12 | 4 | `rx` | int32 | 0.001 度 |
| 16 | 4 | `ry` | int32 | 0.001 度 |
| 20 | 4 | `rz` | int32 | 0.001 度 |

姿態採 ZYX 內旋尤拉角（yaw-pitch-roll）。

**座標系尚未定義。** 原點、三軸方向與手腕姿態的參考方向，待 B 的絕對位置來源確定後補上（見 `open-questions.md` 問題 01）。在此之前 `POSE` 不應被使用。

### 4.3 `GRIPPER`（0x03）— 4 bytes

| 位移 | 大小 | 欄位 | 型別 | 範圍 |
|---:|---:|---|---|---|
| 0 | 4 | `opening` | int32 | 0 = 全閉，10000 = 全開 |

採正規化值而非實際行程，因為夾爪幾何由控制層換算，B 不需要知道。

---

## 5. 回應（選用）

C → B，埠 49201。**預設關閉**，僅在 bring-up 與延遲量測時啟用。

控制流本身是 fire-and-forget，不需要逐包確認；開啟回應是為了讓 B 在開發期確認封包被正確解析，以及量測往返時間。

| 值 | 名稱 | payload |
|---:|---|---|
| `0x80` | `ACK` | uint32 `acked_seq` + uint8 `status` |
| `0x81` | `NACK` | uint32 `seq` + uint8 `reason` |

回應封包的標頭格式與指令包相同，`timestamp_us` 由 C 端依 §2.4 的規則蓋上。**原始指令包的 `timestamp_us` 不另外回傳**——B 以 `acked_seq` 對回自己發送時記下的時間即可，不需要讓封包長度隨之增加。

### 狀態碼

| 值 | 名稱 | 意義 |
|---:|---|---|
| `0x00` | `OK` | 已接受 |
| `0x01` | `NOT_IMPLEMENTED` | 本版不支援此 `cmd_type` |
| `0x02` | `OUT_OF_RANGE` | 超出關節限位，已夾限後執行 |
| `0x03` | `ESTOP_ACTIVE` | 急停中，指令已丟棄 |
| `0x04` | `BAD_CRC` | CRC 不符 |
| `0x05` | `BAD_VERSION` | `version` 或 `magic` 不符 |
| `0x06` | `STALE` | `seq` 早於已接受的最新值，已丟棄 |

---

## 6. 時序與失效行為

### 6.1 指令頻率

目標 **100 Hz**（每 10 ms 一包）。

此值待 W10 實測 UART 往返時間後確認。若六軸 SYNC WRITE 加上回應讀取無法在 10 ms 內完成，降為 50 Hz。B 不需配合修改，發送頻率高於控制層處理能力時，控制層只取最新一包。

### 6.2 失序與丟包

- 收到的 `seq` **小於或等於**目前已接受的最新 `seq` → 丟棄，不重排
- 被判為 `STALE` 而丟棄的封包**不重置 watchdog**
- **watchdog 逾時後清除 `seq` 基準**：之後第一個 `magic`、`version`、CRC 都正確的封包，不論 `seq` 為何都接受，並以它的 `seq` 作為新基準
- 不要求重傳。控制資料過期後無價值
- `seq` 為 uint32，在 100 Hz 下約 497 天才回繞，不處理回繞

**為什麼要清除基準**：B 的橋接程式重新啟動時，`seq` 會從頭算。若只靠「小於等於就丟」，重啟後的封包會全部被當成 `STALE`，手臂一直停在保持狀態，直到新的 `seq` 追上舊值——前一次跑了一小時，就要等一小時。STALE 不重置 watchdog，所以重啟後最多 200 ms，控制層就會逾時並清除基準，接著接受 B 的新封包。B 不需要任何配合。

### 6.3 Watchdog

超過 **200 ms** 未收到任何合法封包（含 `NOP`，不含被判為 `STALE` 的封包）：

1. 手臂**保持當前位置**
2. 扭矩維持開啟
3. 清除 `seq` 基準（見 §6.2）

扭矩若關閉，手臂會因自重垮下。所以此處的「安全」是鎖住不動，不是斷電。

B 在無新資料時應持續送 `NOP` 維持連線，否則 200 ms 後系統會進入保持狀態。

### 6.4 急停

收到 `ESTOP`：立即停止運動、保持位置、拒絕後續所有 `JOINT` / `POSE` / `GRIPPER`，直到收到 `RESUME`。

急停期間仍接受 `NOP`，watchdog 照常運作。

### 6.5 上電初始化

控制層啟動時先讀取六顆馬達的實際角度，以此作為初始目標值，再開始接受外部指令。否則上電瞬間手臂會從當前姿態猛然跳到程式預設值。

---

## 7. 最小發送端範例（Python）

```python
import socket, struct, time

MAGIC, VERSION = 0x5343, 0x01
CMD_JOINT, CMD_NOP = 0x01, 0x00
ADDR = ("127.0.0.1", 49200)

def now_us() -> int:
    # 必須是 Unix epoch 起算的牆鐘微秒，見 §2.4。
    # 不要換成 perf_counter / monotonic：epoch 不同，跨行程相減無意義。
    # Windows 上需 Python >= 3.13，否則解析度只有約 15.6 ms；
    # 卡在舊版時改用 §2.4 的 ctypes 版本。
    return time.time_ns() // 1000

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def build(cmd_type: int, seq: int, payload: bytes = b"") -> bytes:
    header = struct.pack(
        "<HBBIQB3s",
        MAGIC, VERSION, cmd_type, seq,
        now_us(),
        len(payload), b"\x00\x00\x00",
    )
    body = header + payload
    return body + struct.pack("<H", crc16_ccitt(body))

def joint_payload(degrees) -> bytes:
    return struct.pack("<6i", *(int(d * 1000) for d in degrees))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
seq = 0
while True:
    seq += 1
    sock.sendto(build(CMD_JOINT, seq, joint_payload([0, 0, 0, 0, 0, 0])), ADDR)
    time.sleep(0.01)
```

對應的 C 結構（`-Wpadded` 下無填充）：

```c
#pragma pack(push, 1)
typedef struct {
    uint16_t magic;         /* 0x5343 */
    uint8_t  version;       /* 0x01   */
    uint8_t  cmd_type;
    uint32_t seq;
    uint64_t timestamp_us;  /* Unix epoch µs，見 §2.4 */
    uint8_t  payload_len;
    uint8_t  reserved[3];
} upper_header_t;           /* 20 bytes */
#pragma pack(pop)
```

---

## 8. 變更紀錄

| 版本 | 日期 | 變更 |
|---|---|---|
| v1.0 | 2026/11/04 | 初版凍結，交付 B |
| v1.1 | 2026/11/04 | 新增 §2.4 明確規定 `timestamp_us` 的時鐘（Unix epoch 牆鐘 µs）、兩端的取法與禁用的 API、C 端 FILETIME 換算、本學期可量測範圍；§5 補明回應包不回傳原始時間戳；§7 範例改用 `time.time_ns()` 並加註不可替換的理由 |
| v1.2 | 2026/11/05 | §2.1 註明 `magic` 的線上 bytes 為 `43 53`、須以 uint16 比對；§2.3 補 CRC 檢查值與 NOP 範例封包；§2.4 修正 v1.1 的錯誤說法（`time_ns()` 與 `time.time()` 時鐘相同，解析度取決於 Python 版本），改為規定 Windows 上 Python ≥ 3.13 或改用 ctypes，表格補上 LeRobot 版控制層；§6.2、§6.3 新增 STALE 不重置 watchdog、watchdog 逾時後清除 `seq` 基準；§7 註解同步修正 |

變更此規格需同時通知 B。`version` 欄位用於不相容變更；相容的擴充使用 `reserved` 欄位或新增 `cmd_type`。

> v1.1 為**相容性澄清**，封包格式未變，B 已開始的實作不需重寫；但 `timestamp_us` 的取法若與 §2.4 不符需修正，否則 W12 量不到延遲。
>
> v1.2 封包格式同樣未變。B 需要確認兩件事：橋接環境的 Python 版本（§2.4），以及 `magic` 是以 uint16 寫入而不是字串 `b"SC"`（§2.1）。§6.2 的變更只在控制層，B 不需配合。
