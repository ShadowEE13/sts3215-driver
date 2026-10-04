# LeRobot / Feetech SDK 對照筆記

對照目的：協定手冊說「應該怎樣」，LeRobot 說「實際上怎樣才會動」。寫 C 之前先把兩者的落差找出來。

閱讀版本：
- LeRobot：`huggingface/lerobot` main，commit `8c920c4`（2026-10-03）
- Feetech 官方 Python SDK：`ftservo/FTServo_Python`（2026-09-29）。LeRobot 實際裝的是 PyPI 上的非官方版 `feetech-servo-sdk`，兩者大致相同，但 LeRobot 有 monkeypatch 修正它的 timeout bug

---

## 1. 結論先講

1. **封包格式、checksum、little-endian 跟協定筆記完全一致**。手冊裡 15 個範例封包用程式驗算過，14 個正確。
2. **手冊 RESET 範例的 checksum 印錯**：`FF FF 01 02 0A F6` 應為 `F2`。
3. **負數不是二補數，是 sign-magnitude**。這是協定筆記原本沒寫、寫 C 最容易踩的坑（見 4.1）。
4. **LeRobot 的鮑率對照表跟原廠不一致**，以原廠為準（見 4.2）。
5. **LeRobot 遙操作迴圈不檢查馬達回報的錯誤**，這是你安全層可以補的缺口（見 8）。

---

## 2. 呼叫鏈：LeRobot 怎麼分層

```
SOFollower / SOLeader          robots/so_follower/so_follower.py
  校正、安全限幅、遙操作迴圈       teleoperators/so_leader/so_leader.py
        │
FeetechMotorsBus               motors/feetech/feetech.py + tables.py
  暫存器表、sign 編碼、型號差異
        │
SerialMotorsBus                motors/motors_bus.py
  read / write / sync_read / sync_write、重試、正規化
        │
scservo_sdk.PacketHandler      protocol_packet_handler.py
  組包、checksum、收包、timeout
        │
scservo_sdk.PortHandler        port_handler.py（pyserial）
  開 port、讀寫 bytes、計時
```

這個分層幾乎可以直接對應你的 C 驅動：

| LeRobot / SDK | 你的 C 驅動 | 備註 |
|---|---|---|
| PortHandler | **HAL**（序列埠抽象） | 移植 STM32 時只換這層 |
| PacketHandler | Protocol 層 | 組包 / 解包 / checksum |
| FeetechMotorsBus + tables | Device 層 | 暫存器表、單位、sign 編碼 |
| SOFollower | Application 層 | 安全層、遙操作迴圈 |

---

## 3. 值得讀的檔案

| 檔案 | 行數 | 重點 |
|---|---|---|
| `src/lerobot/motors/feetech/tables.py` | 257 | 暫存器表（名稱 → 地址、長度）、sign bit 表、鮑率表 |
| `src/lerobot/motors/feetech/feetech.py` | 459 | `configure_motors`、`broadcast_ping`、校正 |
| `src/lerobot/motors/motors_bus.py` | 1296 | 只需讀 854 行之後：read / write / sync 與正規化 |
| `src/lerobot/motors/encoding_utils.py` | — | sign-magnitude 編解碼（20 行） |
| `tests/mocks/mock_feetech.py` | 460 | **最好的參考**：不靠 SDK、自己手組每種封包 |
| `src/lerobot/robots/so_follower/so_follower.py` | — | SO-101 的 ID 對應、PID 設定、安全限幅 |
| SDK `protocol_packet_handler.py` | 565 | `txPacket` / `rxPacket`：C 版收發邏輯的範本 |
| SDK `sms_sts.py` | 114 | 原廠 STS 高階函式（`WritePosEx`） |

注意 `mock_feetech.py` 中 `write()` 的 docstring 寫地址有 L/H 兩個 byte，那是從 Dynamixel 抄來沒改的；實際程式碼只用 1 個 byte 地址，跟手冊一致。

---

## 4. 協定筆記沒寫、或要修正的地方

### 4.1 負數用 sign-magnitude，不是二補數 ⚠️

最高的「方向位」代表正負，其餘位元是絕對值：

| 暫存器 | 地址 | sign bit |
|---|---|---|
| Goal_Position / Present_Position | 0x2A / 0x38 | BIT15 |
| Goal_Velocity / Present_Velocity | 0x2E / 0x3A | BIT15 |
| Present_Load | 0x3C | BIT10 |
| Homing_Offset（位置校正） | 0x1F | BIT11 |

例：-100 寫入 Goal_Position
- 二補數（**錯**）：`0xFF9C`
- sign-magnitude（**對**）：`0x8000 | 100 = 0x8064` → 送出 `64 80`

C 版本：

```c
static inline uint16_t sm_encode(int32_t v, uint8_t sign_bit) {
    uint16_t mag = (uint16_t)(v < 0 ? -v : v);
    return v < 0 ? (uint16_t)(mag | (1u << sign_bit)) : mag;
}

static inline int32_t sm_decode(uint16_t raw, uint8_t sign_bit) {
    int32_t mag = raw & ((1u << sign_bit) - 1);
    return (raw >> sign_bit) & 1 ? -mag : mag;
}
```

正常單圈位置模式下位置都是 0–4095，碰不到這問題；但讀**速度**、**負載**時一反轉就會出現負值，直接用 `int16_t` 轉型會讀到很大的錯誤數字。

### 4.2 鮑率對照表：LeRobot 跟原廠不一致

| 值 | 原廠 xlsx 與 SDK `sms_sts.py` | LeRobot `tables.py` |
|---|---|---|
| 0–4 | 1M / 500k / 250k / 128k / 115200 | 相同 |
| 5 | **76800** | 57600 |
| 6 | **57600** | 38400 |
| 7 | **38400** | 19200 |

以原廠為準。預設 1Mbps 不受影響，但不要照抄 LeRobot 的表。

### 4.3 「運行時間」0x2C 已釐清

位置模式下填 0。原廠 SDK 的 `WritePosEx` 是從 **0x29（加速度）開始一次寫 7 bytes**：

```
地址:  0x29   0x2A 0x2B   0x2C 0x2D   0x2E 0x2F
內容:  acc    pos_L pos_H  0    0      spd_L spd_H
```

只有 PWM 開環模式（模式 2）才用到 0x2C。這比分開寫加速度、位置、速度省兩包。

### 4.4 Phase 暫存器 0x12 的 BIT4

LeRobot 在 STS3215 上會**清掉 0x12 的 BIT4**，強制位置回授為單圈 0–4095。否則位置可能變成多圈或負值。拿到手臂後先讀這個暫存器確認。

### 4.5 回應延遲 0x07

- 預設 250（= 500µs），LeRobot 會改成 0
- 但原廠 xlsx 寫「STS 此地址無功能」
- **待硬體驗證**：改前改後各量一次 READ 往返時間

### 4.6 其他小落差

| 項目 | 手冊 / xlsx | LeRobot / SDK | 影響 |
|---|---|---|---|
| 型號碼 | 地址 3、4 各 1 byte（主/次版本） | 當成 16-bit 一起讀，sts3215 = **777**（0x0309） | PING 後讀這個可以確認型號 |
| 最大 ID | 253 | SDK `MAX_ID = 0xFC`（252） | 無 |
| 指令 0x0B | 手冊沒有 | SDK 有 `INST_OFSCAL`（偏移校正） | 暫不用 |
| RECOVERY 0x06 | 手冊有 | SDK 沒實作 | 暫不用 |
| 最小啟動力 0x18 | 1 byte | LeRobot 寫 2 bytes | 不碰 |
| 工廠區 0x50–0x56 | xlsx 沒列 | LeRobot 會寫 `Maximum_Acceleration`（0x55）= 254 | 非公開，待確認 |

### 4.7 EPROM 寫入流程（Lock 0x37）

LeRobot 的做法：
- `disable_torque`：Torque_Enable(0x28) = 0，Lock(0x37) = 0
- `enable_torque`：Torque_Enable = 1，Lock = 1

所以要改 EPROM（ID、角度限位、位置校正）：**先關扭力、Lock 寫 0 → 寫入 → Lock 寫 1**。

---

## 5. 收包 parser：直接照這個邏輯寫 C

官方 SDK `rxPacket()` 的流程，處理了雜訊、半包、錯位：

```
1. TX 之前先 clearPort()，清掉 RX buffer 殘留的舊資料
2. 至少收滿 6 bytes（最短回應包：FF FF ID LEN ERR CKS）
3. 找 FF FF：不在開頭，就丟掉前面的垃圾 byte
4. 合理性檢查：ID ≤ 0xFD、Length ≤ 250、Error ≤ 0x7F
   → 不通過就丟 1 byte，回到步驟 3 重新同步
5. 總長 = Length + 4；還沒收滿就繼續收，逾時則回報 RX_TIMEOUT 或 RX_CORRUPT
6. 驗 checksum
7. 回應 ID ≠ 發送 ID → 丟掉、繼續收（txRxPacket 的外層迴圈）
```

回傳碼可以照抄成 C 的 enum：

```c
typedef enum {
    COMM_SUCCESS       =  0,
    COMM_PORT_BUSY     = -1,
    COMM_TX_FAIL       = -2,
    COMM_RX_FAIL       = -3,
    COMM_TX_ERROR      = -4,
    COMM_RX_WAITING    = -5,
    COMM_RX_TIMEOUT    = -6,
    COMM_RX_CORRUPT    = -7,
    COMM_NOT_AVAILABLE = -9,
} comm_result_t;
```

**通訊結果**（comm_result）跟**馬達錯誤**（Error byte）是兩件事：前者是「封包有沒有正確收到」，後者是「馬達本身有沒有出狀況」。API 要分開回傳。

---

## 6. Timeout 與延遲（給第 11–13 週延遲量測）

SDK 的 timeout 公式：

```
tx_time_per_byte = 10 bits / baud        → 1Mbps 時為 0.01 ms/byte
packet_timeout   = (預期長度 + 3) × tx_time_per_byte + 50 ms
```

- 一個 6-byte 回應包在線上只需約 **60µs**
- timeout 卻預留了 **50ms**（`LATENCY_TIMER`），用來吸收 USB 轉序列晶片的延遲

**待驗證的假設**：PC 版的延遲瓶頸在 USB 轉序列，而不是 UART 本身。若量測證實，就是「為什麼要移植到 STM32 直接接 UART」最有力的數據，可以寫進報告。

LeRobot 預設 `num_read_retries = 2`，表示他們實際上會遇到讀取失敗，需要重試。

---

## 7. SO-101 實際怎麼用

**馬達 ID**（leader、follower 相同）

| ID | 關節 |
|---|---|
| 1 | shoulder_pan |
| 2 | shoulder_lift |
| 3 | elbow_flex |
| 4 | wrist_flex |
| 5 | wrist_roll |
| 6 | gripper |

**遙操作資料流**

```
leader:   sync_read("Present_Position")   ← 6 顆一次讀
             ↓ 正規化（-100~100 或角度）
follower: sync_write("Goal_Position")     ← 6 顆一次寫，不等回應
```

**兩種寫入的分工**，你的 API 也可以這樣設計：
- `write()`：等回應包，可靠但慢 → **設定參數用**
- `sync_write()`：廣播、不等回應，快但可能掉包 → **控制迴圈用**

**follower 連線時的設定（`configure`）**
- 回應延遲 0、加速度 254、最大加速度 254、位置模式
- PID 改成 P=16、I=0、D=32（覆蓋出廠值）
- 夾爪額外保護：最大扭矩 500（50%）、保護電流 250、過載扭矩 25%

**校正流程**
1. 手臂擺到中間姿勢 → 寫 Homing_Offset，讓目前位置讀起來剛好是 2047
2. 每個關節手動轉過完整範圍 → 記錄 min / max
3. min / max 寫進馬達的 0x09 / 0x0B（wrist_roll 例外，固定 0–4095）

---

## 8. 安全層可以借鏡、也可以補強的地方

LeRobot 的安全機制有三層：

| 層 | 做法 | 位置 |
|---|---|---|
| 韌體 | 校正範圍寫進 0x09 / 0x0B，**馬達自己拒絕超出範圍** | `write_calibration` |
| 軟體 | `max_relative_target`：限制每步目標與目前位置的差距 | `ensure_safe_goal_position` |
| 夾爪 | 扭矩、電流上限，防止夾住東西時燒掉 | `configure` |

**LeRobot 沒做、你可以補的**：
- `_sync_read`（`motors_bus.py` 1173 行）只檢查通訊結果，**沒有讀每顆馬達回應包裡的 error byte**。官方 SDK 其實有把 error 存起來（`GroupSyncRead.isAvailable()` 的第二個回傳值），只是 LeRobot 沒用。
- 結果：遙操作中馬達過熱、過載，LeRobot 不會察覺。
- `max_relative_target` 預設是 `None`，也就是**預設不開**。

你的安全層若在控制迴圈中檢查 error byte、異常時安全停止，就是比 LeRobot 多出來、可以在報告裡明確寫出的貢獻。

---

## 9. 拿到手臂後要驗證的（補進協定筆記第 6 節）

- [ ] 讀 Model Number（地址 3，長度 2）：7.4V 跟 12V 版是否都回 777？
- [ ] 讀 Phase（0x12）：BIT4 是 0 還是 1？
- [ ] 回應延遲 0x07 改成 0 前後，READ 往返時間有沒有差？
- [ ] USB 轉接板會不會把 TX 回送到 RX（echo）？若會，收包時要先跳過自己送出的那包
- [ ] 量測：單顆 READ、6 顆 SYNC READ、6 顆 SYNC WRITE 各自的往返時間