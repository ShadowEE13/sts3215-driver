# CLAUDE.md — sts3215-driver

給在本 repo 工作的 Claude Code 的說明。請用繁體中文與使用者溝通。

## 專案是什麼

NYCU 電機系大三專題子題目：以純 C11 手寫 Feetech STS3215 伺服馬達的串列通訊驅動，用於 VR 遙操作機械手臂（SO-ARM101，6 軸，leader 7.4V / follower 12V）。

- 協定細節：`docs/protocol-notes.md`、`docs/LeRobot-notes.md`
- 模擬器規格：`docs/sim-spec.md`
- **目前的工作計畫：`docs/week06-08-plan.md`**（步驟 0–11，依序執行）
- 總時程：`docs/schedule.md`

## 分工

- **Claude Code（你）**：撰寫所有程式碼與測試。
- **使用者**：理解程式、做必要決策、commit / push。使用者目前不寫程式，但需要能在 meeting 上解釋每一行，所以程式碼與註解要寫得讓大三學生讀得懂。
- **另一個 Claude 對話**：負責 review、解釋與決策紀錄。每完成一個步驟，使用者會拿去那邊 review。

## 建置環境（Windows）

- 編譯：**MSYS2 UCRT64** 終端機，`gcc` + `make`
- git：Git for Windows（已加進 UCRT64 的 PATH）
- repo 路徑：`C:\GithubRepo\sts3215-driver`（UCRT64 內為 `/c/GithubRepo/sts3215-driver`）
- checkout 為 CRLF（`core.autocrlf`）。不要大量轉換換行，避免整檔 diff

```
make test      # 編譯並執行 tests/test_*.c
make demo      # 編譯並執行 sim/demo.c（只連模擬器，不連驅動）
make layers    # 分層檢查：src/ 不可引用 sim/、termios、unistd、malloc
make SAN=1 test  # ASan + UBSan（MSYS2 若不支援則略過，並告知使用者）
```

`tests/check.h` 是現成的測試框架（`CHECK`、`CHECK_EQ`、`CHECK_BYTES`、`CHECK_SUMMARY`）。新增 `tests/test_xxx.c` 會被 `make test` 自動抓到，不需改 Makefile。

## 硬性規範（不可違反）

- 純 C11；`-Wall -Wextra -Werror -Wconversion` 零警告
- **不使用 `malloc`**；buffer 一律靜態配置或由呼叫者提供
- `src/` 不依賴作業系統。唯一例外是 PC 端 HAL 實作（W10 才寫，Windows 版將為 `src/hal_win32.c`）
- **Protocol 層零全域變數**；所有狀態放在呼叫者提供的 struct（leader / follower 兩條 bus 要能同時存在）
- 所有 header 加 `extern "C"` 包裝
- 時間比較一律 `(uint32_t)(now - start) >= timeout`（處理溢位）
- 負數一律 sign-magnitude（位置/速度 BIT15、負載 BIT10、Homing Offset BIT11），不是二補數

## HAL 介面：已定案的設計決策

`include/sts_hal.h` 依下列決策實作，並把每條理由寫進 header 註解：

1. **`read` 非阻塞**：有多少回多少，沒有就回 0。逾時判斷統一放在 Protocol 層，以 `millis()` 計算
2. **`write` 回傳時代表 bytes 已全部送出、bus 可開始接收**。半雙工方向切換是 HAL 實作的責任，上層不知情
3. **時間**：`uint32_t millis(void *ctx)`，單位毫秒
4. **`ctx`**：每個函式的第一個參數都是 `void *ctx`，Protocol 層只轉交、不解讀
5. **函式表與實例分開**：
   ```c
   typedef struct { /* 4 個函式指標 */ } hal_ops_t;               // 函式表，可宣告為 static const
   typedef struct { const hal_ops_t *ops; void *ctx; } sts_hal_t; // 一條 bus 的實例
   ```
   理由：函式表是 `const`，在 STM32 上可放進 flash；leader 與 follower 共用同一張函式表，各自帶不同的 `ctx`。這也是 Linux kernel `struct file_operations` 的模式
6. **回傳型別**：`write` / `read` 回傳 `int`（實際送出/讀到的 byte 數，`< 0` 代表錯誤），長度參數用 `size_t`。Length 欄位只有 1 byte，整包最長 4 + 255 = 259 bytes（SDK 合理性檢查更限制 Length ≤ 250），遠小於 `INT_MAX`，轉型安全；在 header 註解說明這個前提

## 模擬器與驅動的獨立性（最重要的規則）

模擬器（`sim/`）與驅動（`src/`）都由你撰寫，同一份對協定的誤解可能同時出現在兩邊、互相吻合而不被發現。因此：

- **`sim/` 與 `src/` 不共用任何程式碼**，彼此不 include 對方的 header。兩邊唯一的共同點是 `include/sts_hal.h`
- 模擬器的 parser 刻意寫得簡單直白，不要「重用」驅動的 parser
- **驗收用的封包一律逐 byte 照抄 `docs/protocol-notes.md` 第 4 節**，寫成常數陣列。**不可**用受測程式碼產生預期值
- 驗收順序：A（手冊 → 模擬器）→ B（驅動組包 vs 手冊）→ C（手冊回應 → 驅動解析）→ D（驅動 ↔ 模擬器）。D 不能取代 A/B/C
- RESET 範例：手冊印 `F6`，正確為 `F2`（已驗證），測試以 `F2` 為準

## 工作流程

1. 一次只做 `docs/week06-08-plan.md` 的**一個步驟**
2. 完成後執行 `make test` 與 `make layers`，全部通過才算完成
3. 停下來，給使用者一段簡短摘要：
   - 做了什麼、新增或修改了哪些檔案
   - **在模擬器上自行決定、需要真機驗證的行為**（列成「待真機驗證」項目）
   - 建議的 commit message
4. **不要自行 commit 或 push**。由使用者確認後執行，commit message 格式為 `step N: 簡述`
5. 在 `docs/week06-08-plan.md` 把完成的 checkbox 打勾

## 何時要停下來問使用者

純技術選擇（命名、內部資料結構、錯誤碼數值）自行決定，並在註解寫明理由。以下情況要先問：

- 影響隊友的事：上層 UDP 介面格式、傳輸方式
- 影響範圍或時程：要做的事超出目前步驟，或某步驟明顯做不完
- 協定手冊與 `docs/` 筆記互相矛盾，或手冊沒寫清楚、會影響驅動行為的地方
- 需要修改本檔的硬性規範或 HAL 決策

## 不要做的事

- 不要寫需要真機的東西（`hal_win32.c`、LeRobot 相關），那是 W10 的工作
- 不要修改 `docs/schedule.md`、`docs/lab-log/`、`docs/protocol-notes.md`、`docs/LeRobot-notes.md`（使用者的紀錄）；發現筆記有誤時，告知使用者
- 不要引入第三方函式庫或測試框架
