# 第 6–8 週計畫（補進度）：HAL 介面 + 模擬器 + Protocol 層

**原訂期間**：W6–W8（10/12–11/01），期中考期間未執行
**補做期間**：11/04（三）– 11/08（日），屬學期第 9 週
**相關文件**：`sim-spec.md`（模擬器規格）、`protocol-notes.md`（協定與範例封包）、`LeRobot-notes.md`（parser 流程、sign-magnitude）、`memory-table-check.md`（記憶體表核對）、`schedule.md`（總時程）

---

## 一、補進度範圍

原 W6–W8 的工作分成兩類，這次只補不需要硬體的部分。需要真機的工作集中到 W10 手臂到手後，在同一個實驗室場次完成。

| 原訂內容 | 原週次 | 本次處理 |
|---|---|---|
| 定義 `sts_hal.h`、模擬器實作 | W6 | ✅ 11/04 完成 |
| C 單軸實作：組封包、checksum、WRITE、READ 解析 | W6–7 | 組包 ✅ 11/04；解析進行中 |
| 硬性檢查點：自算 checksum 與官方一致 | W6–7 | ⚠️ 已以手冊範例封包驗證（驗收 B）；與真機擷取封包比對留至 W10 |
| LeRobot 送指令 + 擷取真實封包 | W6–7 | ⏭ 移至 W10（需真機） |
| 逾時處理、重傳、錯誤碼解析 | W8 | 11/06–11/08 |
| 六軸同步控制（SYNC READ / SYNC WRITE） | W8 | 模擬器端 ✅ 11/04 提前完成；驅動端 11/06–11/08；真機驗證留至 W10 |

---

## 二、核心原則

**先讓模擬器對上手冊，再讓驅動對上手冊，最後才讓兩者互打。**

模擬器與驅動若出自同一份協定理解，錯誤會互相吻合而不被發現。在拿到真機之前，手冊範例封包是唯一的外部參照，所以驗收分成四步（A–D）：前三步都直接對照手冊，第四步才讓兩邊互打。

---

## 三、第 6 週內容 → 11/04（三）HAL 介面 + 模擬器

### 步驟 0：Repo 骨架（約 30 分）

- [x] 建立目錄：`include/`、`src/`、`sim/`、`tests/`
- [x] Makefile 從第一天就開 `-std=c11 -Wall -Wextra -Werror -Wconversion`（太晚開會一次冒出大量警告）
- [x] 測試框架：自寫約 10 行的 `CHECK()` 巨集，不引入第三方

### 步驟 1：`include/sts_hal.h`（約 1–2 小時，多數時間在設計）

以下四項已決定，答案寫進 header 註解（另有第五項「函式表與實例分開」，見第七節）：

| 設計問題 | 決定 | 理由 |
|---|---|---|
| `read` 阻塞或非阻塞？ | **非阻塞**：有多少回多少，沒有就回 0 | 逾時判斷統一放 Protocol 層（用 `millis()`），只寫一次，三種 HAL 共用。STM32 以中斷/DMA 收進 ring buffer，本來就是非阻塞形狀 |
| `write` 回傳代表什麼？ | **bytes 已全部送出，bus 可開始接收** | 半雙工方向切換是 HAL 實作的責任，上層不知情。PC 由轉接板處理；STM32 需等 TC 旗標再切換。契約寫明，移植時不動 Protocol 層 |
| 時間比較方式？ | `uint32_t` 毫秒；一律寫 `(uint32_t)(now - start) >= timeout` | 計數器溢位時仍正確 |
| `ctx` 用途？ | 所有狀態放在呼叫者提供的 struct，Protocol 層**零全域變數** | leader 與 follower 是兩條 bus，同一份程式碼要能同時開兩個實例 |

介面組成：`hal_ops_t` 為 `write` / `read` / `flush_rx` / `millis` 四個函式指標（每個的第一個參數都是 `void *ctx`），宣告成 `static const`；`sts_hal_t` = `ops` 指標 + `ctx`，代表一條 bus。另提供 `hal_write()` 等 inline 包裝與 `hal_timed_out()`。

- [x] `include/sts_hal.h` 完成，六項決策理由寫入 header 註解
- [x] `tests/test_hal.c`：計數器溢位判斷、兩條 bus 共用 const 函式表且 ctx 互不干擾（26 項）

### 步驟 2：`sim/servo_sim`：Control Table + 指令處理（約 3 小時）

- [x] 每顆馬達一個 128-byte 陣列，每個位址標註屬性（唯讀 / EPROM / SRAM），WRITE 依屬性決定是否接受
- [x] EPROM 寫入檢查 Lock（0x37）——**語意與直覺相反，見 F-15**
- [x] 支援 PING / READ / WRITE；廣播 ID 0xFE 僅 PING 回應
- [x] Model Number（0x03，2 bytes）回 777
- [x] **模擬器的 parser 不與驅動共用程式碼**：刻意寫得簡單直白，共用就失去互相驗證的意義
- [x] 測試鉤子 `sim_poke(id, addr, value)`：直接設定暫存器。驗收會用到，例如範例 4.2 的回應位置是 1304，須先將 0x38 設為 1304，模擬器才回得出 `18 05 DD`
- [x] 提前完成：SYNC READ / SYNC WRITE、應答級別 0x08、`sim_power_cycle()`

### 步驟 3：`sim/hal_sim`（約 1 小時）

- [x] `write`：將 bytes 交給 servo_sim，回應放進 RX 佇列
- [x] `read`：從 RX 佇列取資料
- [x] **虛擬時鐘**：`millis()` 回傳模擬時間，不使用真實時間

> ⚠️ 陷阱：馬達不回應時，Protocol 層會在 `while (!逾時) read()` 中空轉；若虛擬時鐘不前進，就是無窮迴圈。
> 做法：每次 `read` 讀不到資料時，虛擬時間前進 1 ms。逾時測試因此是確定性的，且瞬間跑完。

### 步驟 4：運動模型最小版（約 1 小時）

- [x] 每個 tick 依 Goal_Velocity（0x2E）朝 Goal_Position（0x2A）前進
- [x] 受 Min/Max_Position_Limit（0x09 / 0x0B）限制
- [x] Torque_Enable（0x28）為 0 時不動
- [x] 同步更新 Present_Position（0x38）、Present_Speed（0x3A）、Moving（0x42）
- [x] tick 綁定在虛擬時鐘上
- [x] Goal_Velocity = 0 的行為由 Phase（0x12）BIT3 決定，見 F-17
- [x] 規格未定義的行為（例：扭矩關閉時寫入目標位置，真機是否自動開扭矩？）→ 先選一種實作，並列入 `sim/README.md` 的「待真機驗證」清單，不默默決定

### ✅ 驗收 A：模擬器 vs 手冊

> **11/04 通過**：`make demo` 169/169（含驗收 A 三個範例逐 byte 相符、RESET checksum 為 `F2`）。另經 gcc / clang / ASan+UBSan 交叉編譯無警告、無記憶體錯誤。

`sim/demo.c` 以手組 byte 陣列將範例 4.1 / 4.2 / 4.3 餵入模擬器，回應與手冊**逐 byte 相同**。此步不經過驅動。

| 範例 | 發送 | 預期回應 | 前置條件 / 事後檢查 |
|---|---|---|---|
| 4.1 PING | `FF FF 01 02 01 FB` | `FF FF 01 02 00 FC` | — |
| 4.2 READ 0x38 | `FF FF 01 04 02 38 02 BE` | `FF FF 01 04 00 18 05 DD` | 先 `sim_poke(1, 0x38, 1304)` |
| 4.3 WRITE 0x2A | `FF FF 01 09 03 2A 00 08 00 00 E8 03 D5` | `FF FF 01 02 00 FC` | 事後 0x2A 應為 2048、0x2E 應為 1000 |

---

## 四、第 7 週內容 → 11/05（四）Protocol 層 + 單軸讀寫

### 步驟 5：純函式 + 單元測試（約 2 小時）

- [x] `checksum`、`build_packet`
- [x] `sm_encode` / `sm_decode`（sign-magnitude，見 `LeRobot-notes.md` 4.1）
- [x] little-endian 組合 / 拆解

測資：

| 測試 | 預期 | 驗證什麼 |
|---|---|---|
| 4.1–4.3 發送包 | 與手冊 byte-for-byte 相同 | **驗收 B**：驅動編碼 |
| RESET `FF FF 01 02 0A ??` | checksum = `F2`（手冊印 `F6` 有誤，見 F-02） | 程式依公式計算，非照抄手冊 |
| sign-magnitude −100（BIT15） | `64 80` | 負數編碼（F-01） |

### ✅ 驗收 B：驅動組包 vs 手冊

> **11/04 通過**：`test_protocol` 20 項、`test_device` 653 項全數通過，驅動組出的 4.1–4.3 指令包與手冊逐 byte 相同。
> **反向驗證**：故意把 checksum 改成連 `FF FF` 一起加，驗收 B 有 4 項失敗，證明測試對這類錯誤有鑑別力。
> **獨立對照**：另以 Python 依手冊公式重寫組包與 sign-magnitude，與 C 版結果一致（含 248 參數的最大封包、`INT32_MIN` 等極端值）。

至此「協定理解正確」已有兩道獨立的外部證據（A 從模擬器側、B 從驅動側，兩者都對照手冊而非彼此）。

### 步驟 6：收包狀態機（約 2–3 小時，這幾天最難的部分）

- [ ] 依 `LeRobot-notes.md` 第 5 節 7 步驟實作
- [ ] 設計為**一次餵一個 byte** 的狀態機（直接對應 STM32 UART RX 中斷，移植時可原樣使用）
- [ ] API 將 **comm_result**（封包是否正確收到）與 **error byte**（馬達是否異常）分開回傳
- [ ] echo 由 HAL 濾除（F-20：轉接板回送的 PING 封包各欄位全部合法，Instruction 會被誤判為 error byte）

測資（不需模擬器，直接餵 byte 陣列）—**驗收 C**：

- [ ] 範例 4.2 回應 → error = 0、位置 = 1304
- [ ] 前方插入垃圾 byte → 仍能同步並解出
- [ ] 封包切成兩半分次餵 → 正確組回
- [ ] checksum 改錯一個 bit → 回報 `COMM_RX_CORRUPT`
- [ ] Error byte > 0x7F → 丟棄並重新同步

### 步驟 7：txrx 與單軸 API（約 1–2 小時）

- [ ] `sts_ping` / `sts_read` / `sts_write`
- [ ] 流程：`flush_rx` → `write` → 收包至完成或逾時 → 比對回應 ID
- [ ] 分層自我檢查：`make layers`——`src/` 內 grep `sim/` 與平台 API 必須零筆

### ✅ 驗收 D：驅動 ↔ 模擬器

- [ ] PING → READ 0x38 → WRITE 目標 2048 → 跑數個 tick → 再 READ，確認位置朝目標前進
- [ ] 對不存在的 ID 下指令 → 回傳 `COMM_RX_TIMEOUT`，且程式不卡住

---

## 五、第 8 週內容 → 11/06（五）– 11/08（日）可靠性 + 六軸

### 步驟 8：逾時與重傳

- [ ] 逾時值依 `LeRobot-notes.md` 第 6 節公式：`(預期長度 + 3) × 每 byte 傳輸時間 + 餘裕`；餘裕設為參數，不寫死（W12 調 latency timer 時要改）
- [ ] 重傳次數可設定（LeRobot 預設讀取重試 2 次）
- [ ] 只有通訊失敗才重傳；收到回應但 error byte 非 0 **不重傳**，直接回報

### 步驟 9：錯誤碼解析

- [ ] 將 error byte / 0x41 的各 bit 解成具名旗標（電壓、磁編碼、過熱、過流、過載，見 `protocol-notes.md` 5.3）
- [ ] 提供轉文字的函式，給 log 與日後安全層使用

### 步驟 10：模擬器故障注入的對應測試

模擬器端的注入功能已於 11/04 實作，本步驟補上驅動側的測試：

- [ ] 指定馬達不回應 → `COMM_RX_TIMEOUT`
- [ ] 強制 error byte 任一位元 → 封包收取成功但回報對應旗標
- [ ] 下一包 checksum 故意算錯 → `COMM_RX_CORRUPT`
- [ ] 回應前後插入雜訊 byte → 仍能重新同步
- [ ] SYNC READ 時讓中途一顆不回應 → 其餘馬達的結果不受影響

### 步驟 11：SYNC READ / SYNC WRITE（驅動側）

- [ ] 先在 `demo.c` 手組封包餵模擬器，對照手冊附的 SYNC 範例（同樣是 A → B/C → D 的順序）
- [ ] SYNC READ：回應依 ID 順序逐包返回，**每顆馬達各自回報結果**，一顆失敗不應讓整次讀取作廢
- [ ] SYNC READ：保留每顆的 error byte（LeRobot 沒做的部分，見 F-06）
- [ ] SYNC WRITE：廣播、無回應，bus 上無法確認是否成功 → 在模擬器上以事後讀回暫存器驗證

### ✅ 第 6–8 週補進度完成條件

- [x] 驗收 A 通過
- [x] 驗收 B 通過
- [ ] 驗收 C、D 通過
- [ ] 六軸 SYNC READ / SYNC WRITE 的驅動側在模擬器上通過
- [ ] 每種故障注入都有對應的驅動側測試且通過
- [x] `make` 無警告

> 模擬器上的六軸不等於完成。封包結構與同步邏輯先在這裡驗證完，W10 真機時間就能專注在只有真機會出現的問題：匯流排競爭、負載下的時序、掉包。

---

## 六、驗收結構總表

| 驗收 | 比對 | 抓得到的錯誤 | 狀態 |
|---|---|---|---|
| A | 手冊發送包 → 模擬器 → 比對手冊回應包 | 模擬器的協定理解 | ✅ |
| B | 驅動組包 vs 手冊 | 驅動的編碼 | ✅ |
| C | 手冊回應包 → 驅動解析 | 驅動的解碼 | 進行中 |
| D | 驅動 ↔ 模擬器 | 整合、逾時流程 | 未開始 |

A、B、C 以手冊為外部參照；D 只證明兩邊彼此同意。例如兩邊都誤以為 checksum 要包含 `FF FF`，D 照樣通過，只有 A/B/C 會失敗。**D 必須排在最後，且不能取代前三者。**

---

## 七、分工

**11/04 調整**：程式碼全部交由 Claude Code（在本機 repo 中執行）撰寫；本人負責理解、設計決策與 commit；另一個 Claude 對話負責 review 與解釋。規則寫在 repo 根目錄的 `CLAUDE.md`。之後有時間再自行重寫核心部分（優先：收包狀態機）。

| 角色 | 負責 |
|---|---|
| Claude Code（本機） | 所有程式碼與測試，一次一個步驟 |
| 本人 | 理解每一步、必要決策、commit / push、meeting 報告 |
| Claude 對話 | 每步 review、解釋、確認理解的問題、決策紀錄 |

**每步流程**：Claude Code 完成一步 → `make test` + `make layers` 通過 → 本人 review 並 commit（`step N: ...`）→ 回到 Claude 對話 review 與解釋 → 下一步。

**同源風險升高**：模擬器與驅動現在出自同一個模型，原本「不同作者」的保險消失。因此驗收 A/B/C 成為唯一的外部防線，不可省略；`sim/` 與 `src/` 不共用任何程式碼，驗收封包一律逐 byte 照抄手冊範例。此外已補上三道額外防線：反向驗證（故意寫錯確認測試抓得到）、Python 獨立重寫對照、原廠記憶體表逐址核對（已因此抓出 F-15）。

**HAL 設計決策（11/04 定案）**：函式表與實例分開（`hal_ops_t` 為 `const` 函式表，`sts_hal_t` = `ops` + `ctx`）；`write`/`read` 回傳 `int`、長度用 `size_t`。完整理由見 `CLAUDE.md`。

---

## 八、留給 W10 真機場次

本次無法補、需要真機的原 W6–W8 項目：

- [ ] `tools/snapshot.py`：**最先執行**，取 LeRobot 動過之前的唯讀暫存器快照（出廠值一旦被改寫就取不回）
- [ ] 跑通 LeRobot 官方範例，建立「已知可運作」基準
- [ ] 擷取 PING / READ / WRITE / SYNC 參考封包並存檔
- [ ] **原硬性檢查點**：自算 checksum 與真機擷取封包逐 byte 一致
- [ ] 實作 `hal_win32.c`，自製驅動接上真機：先單軸，再六軸
- [ ] 逐條確認 V-01～V-18 與 `sim/README.md` 的待驗證清單（V-06 echo 因 F-20 升為高優先）
