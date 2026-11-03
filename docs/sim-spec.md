# 虛擬 STS3215 模擬器 — 實作規格

這份是給實作用的交接文件。背景與技術細節見同資料夾的 `protocol-notes.md`、`lerobot-notes.md`。

---

## 1. 為什麼要做

手臂只有一台，在實驗室，要預約。模擬器讓非使用時間也能在家開發與回歸測試，把預約到的時間留給只有真機能做的事（延遲量測、時序驗證）。

同時它也驗證了架構分層：如果 HAL 介面設計得乾淨，換成模擬器應該一行上層程式碼都不用改。

## 2. 範圍

**要做**：模擬器（虛擬馬達 + 假的 HAL 實作）與其示範程式。

**不要做**：Protocol / Device / Application 三層。那是專題本體，由開發者自己實作，模擬器是被測試的對象。

示範程式用手動組出的 byte 陣列直接餵進模擬器，不依賴尚未存在的驅動層。

## 3. 能測與不能測

| 能測 | 不能測（需真機） |
|---|---|
| 封包組裝：checksum、Length、byte order | 真實延遲：latency timer、USB 排程、回應延遲 0x07 |
| 封包解析：Status Packet、error byte | 半雙工收發方向切換時序 |
| 收包狀態機：雜訊、半包、錯位重新同步 | USB 轉接板的 echo 行為 |
| 逾時與重傳邏輯 | 馬達韌體與手冊不一致之處 |
| sign-magnitude 編解碼 | 電氣與力學行為 |
| Control Table 讀寫語意、EPROM lock | 過載保護的真實觸發 |
| 安全層的錯誤分支（可注入過熱／過載／過流） | |
| 六軸 SYNC READ／SYNC WRITE 封包結構 | |

模擬器測「程式邏輯對不對」，真機測「對硬體的理解對不對」。README 要把這張表寫進去。

## 4. 架構

```
Application / Device / Protocol     ← 不知道自己在跟誰講話
         ↓ hal_ops_t
   ┌─────┴─────┐
hal_pc.c    hal_sim.c
   ↓            ↓
 真實手臂    servo_sim.c
```

`hal_ops_t` 是一組函式指標：`write` / `read` / `flush_rx` / `millis`，每個函式的第一個參數都是 `void *ctx`。函式表宣告成 `static const`，與實例分開：`sts_hal_t` = `ops` 指標 + `ctx`，代表一條 bus（11/04 定案，理由見 `include/sts_hal.h` 註解）。PC 版、STM32 版、模擬器版各自實作同一組。

## 5. 檔案

```
include/sts_hal.h      HAL 介面定義（PC / STM32 / sim 共用，本來就要寫）
sim/servo_sim.h/.c     虛擬馬達：Control Table + 協定處理 + 故障注入
sim/hal_sim.h/.c       假的 HAL，接到 servo_sim
sim/demo.c             手動組封包的示範與自我驗證
sim/README.md          能測什麼、不能測什麼
Makefile
```

## 6. 模擬器要實作的行為

**協定**（細節見 `protocol-notes.md`）
- 字頭 `FF FF`、ID、Length = N+2、Instruction、Params、Checksum = `~(ID+Length+Inst+Params) & 0xFF`
- 支援 PING(0x01)、READ(0x02)、WRITE(0x03)、SYNC READ(0x82)、SYNC WRITE(0x83)
- 廣播 ID 0xFE：除 PING 外不回應
- 多位元組 little-endian
- 位置／速度 sign bit 在 BIT15，負載在 BIT10

**Control Table**
- 完整的位址表在 `protocol-notes.md` 第 5 節
- Model Number（位址 3，長度 2）回 777（0x0309）
- EPROM 區寫入要檢查 Lock(0x37)
- 回授區（0x38 起）由運動模型更新

**運動模型**
- 每個 tick 依 Goal_Velocity(0x2E) 朝 Goal_Position(0x2A) 前進
- 受 Min/Max_Position_Limit(0x09/0x0B) 限制
- Torque_Enable(0x28) 為 0 時不動
- 同步更新 Present_Position / Speed / Load / Moving

**故障注入（這是模擬器最大的價值）**
- 指定馬達不回應（測逾時與重傳）
- 強制 error byte 的任一位元（測安全層分支）
- 下一包 checksum 故意算錯（測 parser 拒收）
- 在回應前後插入雜訊 byte（測重新同步）
- SYNC READ 時讓中途一顆不回應

## 7. 實作限制

與驅動層同樣的規矩，因為 Protocol 層之後要搬到 MCU：

- 純 C11
- `-Wall -Wextra -Werror -Wconversion`
- 不使用 `malloc`，所有 buffer 靜態配置或由呼叫者提供
- 不依賴作業系統概念
- header 加 `extern "C"` 包裝

## 8. 驗收

- `make && ./demo` 可執行，示範 PING / READ / WRITE / SYNC READ 四種操作並印出收送的 byte
- demo 中手動算出的 checksum 與模擬器驗證結果一致
- 可用 `protocol-notes.md` 第 4 節的三個範例封包直接餵入，模擬器回應符合手冊
- 編譯無警告
