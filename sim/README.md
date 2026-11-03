# sim/ — 虛擬 STS3215

手臂採預約制，模擬器讓非預約時段也能開發與回歸測試。它同時驗證 HAL 分層：Protocol 層透過 `sts_hal_t` 跟模擬器講話，換成真機時一行都不必改。規格見 [`docs/sim-spec.md`](../docs/sim-spec.md)。

| 檔案 | 內容 |
|---|---|
| `servo_sim.h/.c` | 虛擬馬達：Control Table、PING / READ / WRITE / SYNC READ / SYNC WRITE、運動模型、故障注入 |
| `hal_sim.h/.c` | `hal_ops_t` 的模擬器實作，虛擬時鐘 |
| `demo.c` | 驗收 A（手冊範例封包逐 byte 比對）＋模擬器自我檢查，`make demo` |

模擬器**刻意不和驅動共用程式碼**（checksum、sign-magnitude 各寫一次）。共用的話，兩邊的錯誤會互相吻合，就測不出來了。

## 用法

```c
#include "hal_sim.h"

sim_bus_t bus;
hal_sim_t hs;

sim_init(&bus);
for (uint8_t id = 1; id <= 6; id++) sim_add_servo(&bus, id);
hal_sim_init(&hs, &bus);
sts_hal_t hal = { &hal_sim_ops, &hs };     // 交給 Protocol 層

sim_poke(&bus, 1, SIM_ADDR_PRESENT_POS, 1304);   // 直接設定暫存器
hal_sim_advance(&hs, 500);                       // 虛擬時間前進 500 ms，馬達跟著動
sim_peek(&bus, 1, SIM_ADDR_GOAL_POS);            // 直接讀暫存器
```

**虛擬時鐘**：`millis()` 回傳模擬時間。`read` 讀不到資料時時間前進 1 ms，所以「馬達不回應」時 Protocol 層的等待迴圈會自然逾時，不會卡死，而且逾時測試瞬間跑完、結果固定。

## 能測與不能測

| 能測 | 不能測（需真機） |
|---|---|
| 封包組裝：checksum、Length、byte order | 真實延遲：latency timer、USB 排程、回應延遲 0x07 |
| 封包解析：Status Packet、error byte | 半雙工收發方向切換時序 |
| 收包狀態機：雜訊、半包、錯位重新同步 | USB 轉接板的 echo 行為 |
| 逾時與重傳邏輯 | 馬達韌體與手冊不一致之處 |
| sign-magnitude 編解碼 | 電氣與力學行為 |
| Control Table 讀寫語意、EPROM lock 與斷電保存 | 過載保護的真實觸發 |
| 安全層的錯誤分支（可注入過熱 / 過載 / 過流） | |
| 六軸 SYNC READ / SYNC WRITE 封包結構 | |

模擬器測「程式邏輯對不對」，真機測「對硬體的理解對不對」。

## 故障注入與測試參數

每顆馬達的 `fault` 欄位直接改，例：`sim_find(&bus, 3)->fault.mute = true;`

| 欄位 | 效果 | 用來測 |
|---|---|---|
| `fault.mute` | 永遠不回應 | 逾時；SYNC READ 中途一顆不回應 |
| `fault.drop_next = N` | 接下來 N 個回應不送，之後恢復 | 重傳後成功 |
| `fault.force_error = bits` | OR 進每個回應的 error byte | 安全層分支、錯誤碼解析 |
| `fault.corrupt_next = N` | 接下來 N 個回應的 checksum 翻一個 bit | parser 拒收（`COMM_RX_CORRUPT`） |
| `sim_inject_noise(&bus, pre, n, post, m)` | 下一次回應前後插入雜訊，只作用一次 | 重新同步 |
| `sim_power_cycle(&bus, id)` | 斷電再上電：EPROM 區還原成 flash 保存的值、SRAM 回到上電預設、位置保留 | Lock 語意、改 EPROM 後是否保存、重開後的初始化流程 |

`hal_sim_t` 上的參數：

| 欄位 | 效果 | 用來測 |
|---|---|---|
| `latency_ms` | 回應在 write 之後多久才讀得到 | 逾時邊界；逾時後才到的舊回應汙染下一包（`flush_rx` 清不掉還在路上的 byte） |
| `rx_chunk` | 每次 `read` 最多回幾 byte | 半包、分段收包 |
| `tx_count`、`tx_last` | write 次數、最後一次送出的內容 | 重傳次數、SYNC WRITE 封包內容 |

「回應」指一個 Status Packet；SYNC READ 時每顆馬達各算一個。

## 讀寫屬性

已與原廠記憶體表逐段核對，見 [`docs/memory-table-check.md`](../docs/memory-table-check.md) 第一節。

| 位址 | 屬性 | 寫入時 |
|---|---|---|
| 0x00–0x04 | 唯讀（版本、型號） | 忽略 |
| 0x05–0x27 | EPROM | 立即生效；Lock = 0 時同時存進 flash |
| 0x28–0x31 | SRAM | 立即生效，斷電消失（0x31 是 0x30 的高位元組，F-16） |
| 0x32–0x36 | 無定義（原廠表權限「無」） | 忽略 |
| 0x37 | SRAM（Lock） | 立即生效 |
| 0x38– | 唯讀（回授區、原廠表未列的位址） | 忽略 |

**Lock（0x37）控制的是「要不要存進 flash」，不是「能不能寫」**（F-15）：Lock = 1 時寫 EPROM 照樣立即生效，只是 `sim_power_cycle` 後會恢復成舊值。

## 預設值（`sim_add_servo`）

標「原廠表」的取自原廠記憶體表的初始值；其餘是模擬器的選擇。原廠表沒給的 EPROM 欄位（電壓門檻、PID、保護電流…）為 0。

| 暫存器 | 值 | 來源 |
|---|---|---|
| Model Number 0x03 | 777 | LeRobot / SDK |
| 鮑率 0x06 / 回應延遲 0x07 / 應答級別 0x08 | 0（1 Mbps）/ 250 / 1 | protocol-notes |
| 角度限制 0x09 / 0x0B | 0 / 4095 | protocol-notes |
| 最高溫度 0x0D | 70 | 原廠表 |
| 最大扭矩 0x10 | 1000 | 模擬器 |
| Phase 0x12 | 0（速度 0 = 停止） | 模擬器（V-14） |
| 卸載條件 0x13 / LED 報警 0x14 | 44 / 47 | 原廠表 |
| 不靈敏區 0x1A / 0x1B、角度分辨率 0x1E | 1 / 1、1 | 原廠表 |
| 保護扭矩 0x22 / 保護時間 0x23 / 過載扭矩 0x24 / 過流保護時間 0x26 | 20 / 200 / 80 / 200 | 原廠表 |
| 扭矩開關 0x28 | 0（關） | 模擬器 |
| 轉矩限制 0x30 | = 0x10（上電時複製） | protocol-notes |
| Lock 0x37 | 0 | 原廠表 |
| 當前位置 0x38 / 目標位置 0x2A | 2048 / 2048 | 模擬器 |
| 電壓 0x3E / 溫度 0x3F | 120（12.0 V）/ 25 °C | 模擬器 |

## 模擬器自己做的決定（待真機驗證）

手冊沒寫清楚的行為，模擬器先選一種實作。W10 拿到手臂後逐條確認，不一致就改模擬器。括號內是 lab log 的編號。

- [ ] **扭矩關閉時寫目標位置**：不會自動開扭矩，扭矩打開後才朝目標移動
- [ ] **上電預設扭矩關**（Lock = 0 已由原廠表確認）
- [ ] **Lock = 1 時寫 EPROM**：立即生效，斷電後恢復原值；Lock = 0 時斷電保留（原廠表文字，V-13）
- [ ] **斷電重開**：當前位置保留、目標位置 = 當前位置、0x30 由 0x10 複製、0x41 狀態清為 0
- [ ] **寫唯讀或無定義位址（0x32–0x36）**：靜靜忽略，應答照常、error = 0
- [ ] **0x47 以後**：原廠表未列，模擬器當唯讀；LeRobot 會寫 0x50–0x55（V-15）
- [ ] **Goal_Velocity = 0**：依 Phase（0x12）BIT3，0 = 停止、1 = 以 `SIM_MAX_SPEED`（3000 步/s）移動；超過 3000 也截在 3000（F-17、V-14）。驅動端永遠明確寫入速度，不依賴這個行為
- [ ] **目標超出角度限制**：停在限制值，Goal_Position 暫存器保留寫入的原值
- [ ] **廣播 PING**：每顆依 ID 順序回應、不碰撞（真機多顆時不能用，見 protocol-notes 第 1 節）
- [ ] **改 ID（0x05）**：該次 WRITE 的應答已用新 ID
- [ ] **READ 超出 Control Table（位址 + 長度 > 128）**：不回應
- [ ] **未實作的指令**（REG WRITE、ACTION、RECOVERY、RESET）：不回應
- [ ] **應答級別 0x08 = 0**：WRITE 不回應，PING / READ 照回（原廠表已確認語意）
- [ ] **ID 重複**：只有第一顆回應
- [ ] **Error byte**：= 0x41 舵機狀態 | `force_error`

## 不模擬

- 加速度 0x29（瞬間達到目標速度）、運行時間 0x2C、扭矩限制、PID
- Homing Offset 0x1F、Torque_Enable = 128（將當前位置校正為 2048）、多圈模式（Phase BIT4、角度限制皆 0）
- 保護機制本身（0x13 卸載條件、0x22–0x26 保護參數只存值，不會真的觸發卸載）；要測保護分支用 `force_error` 或 `sim_poke` 0x41
- 負載 0x3C、電流 0x45、電壓、溫度：維持 `sim_poke` 設定的值，不隨運動變化
- 傳輸時間與回應延遲 0x07：write 瞬間完成；需要延遲用 `latency_ms`
- 鮑率：改 0x06 只存值，不影響通訊
- 扭矩關閉時被外力推動
