# 程式慣例與設計決策

**範圍**：本檔規定 `sts3215-driver` 各目錄的程式撰寫規範、已定案的設計決策，以及建置與測試方式。
決策的完整理由見 `architecture.md`；協定細節見 `protocol-notes.md`。

---

## 1. 適用範圍

| 目錄 | 語言 | 適用章節 |
|---|---|---|
| `src/`、`sim/`、`tests/` | C11 | §3 硬性規範、§4 HAL、§5 模擬器獨立性 |
| `pyctrl/` | Python | §6 控制層 |
| `tools/` | Python | §7 量測與輔助工具 |

C 的硬性規範**不適用**於 Python，反之亦然。兩邊的規則不同是刻意的，理由寫在各自章節。

---

## 2. 建置與測試（Windows）

- 編譯：**MSYS2 UCRT64** 終端機，`gcc` + `make`
- git：Git for Windows（已加進 UCRT64 的 PATH）
- 換行一律 LF（`.gitattributes` 規定 `eol=lf`）。不要大量轉換換行，避免整檔 diff
- 從零設定的步驟與每個「不要」的理由見 `dev-setup.md`

```
make test        # 編譯並執行 tests/test_*.c 與 sim/demo.c
make demo        # 只連模擬器，不連驅動
make layers      # 分層檢查：src/ 不可引用 sim/、平台 API、malloc
make strict      # 加嚴檢查：額外警告 + 以 C++ 編譯器逐一 include 每個 header
make SAN=1 test  # ASan + UBSan
```

`tests/check.h` 是現成的測試框架（`CHECK`、`CHECK_EQ`、`CHECK_BYTES`、`CHECK_SUMMARY`）。
新增 `tests/test_xxx.c` 會被 `make test` 自動抓到，不需改 Makefile。

Python 在 MSYS2 bash 裡不在 PATH 上，要跑 `tools/` 或 `pyctrl/` 的腳本改用
Miniforge Prompt（`conda activate lerobot`）。

---

## 3. C 的硬性規範（`src/`、`sim/`、`tests/`）

- 純 C11；`-Wall -Wextra -Werror -Wconversion` 零警告，gcc 與 clang 都要編過
  （兩者 `-Wconversion` 的檢查範圍不同，F-21）
- **不使用 `malloc`**；buffer 一律靜態配置或由呼叫者提供
- `src/` 不依賴作業系統。唯一例外是 PC 端 HAL 實作（Windows 版為 `src/hal_win32.c`，
  已依 C-26 移至下學期）
- **Protocol 層零全域變數**；所有狀態放在呼叫者提供的 struct
  （leader / follower 兩條 bus 要能同時存在）
- 所有 header 加 `extern "C"` 包裝
- 時間比較一律 `(uint32_t)(now - start) >= timeout`（處理溢位）
- 負數一律 sign-magnitude，**方向位依暫存器而定**：位置／速度 BIT15、負載 BIT10、
  Homing Offset BIT11。不是二補數，也不是一律最高位
- 不引入第三方函式庫或測試框架
- 縮排 4 格

---

## 4. HAL 介面：已定案的設計決策

`include/sts_hal.h` 依下列決策實作，每條理由寫進 header 註解：

1. **`read` 非阻塞**：有多少回多少，沒有就回 0。逾時判斷統一放在 Protocol 層，以 `millis()` 計算
2. **`write` 回傳時代表 bytes 已全部送出、bus 可開始接收**。半雙工方向切換是 HAL 實作的責任，
   上層不知情
3. **時間**：`uint32_t millis(void *ctx)`，單位毫秒
4. **`ctx`**：每個函式的第一個參數都是 `void *ctx`，Protocol 層只轉交、不解讀
5. **函式表與實例分開**：
   ```c
   typedef struct { /* 4 個函式指標 */ } hal_ops_t;               // 函式表，可宣告為 static const
   typedef struct { const hal_ops_t *ops; void *ctx; } sts_hal_t; // 一條 bus 的實例
   ```
   理由：函式表是 `const`，在 STM32 上可放進 flash；leader 與 follower 共用同一張函式表，
   各自帶不同的 `ctx`。這也是 Linux kernel `struct file_operations` 的模式
6. **回傳型別**：`write` / `read` 回傳 `int`（實際送出／讀到的 byte 數，`< 0` 代表錯誤），
   長度參數用 `size_t`。Length 欄位只有 1 byte，整包最長 4 + 255 = 259 bytes
   （SDK 合理性檢查更限制 Length ≤ 250），遠小於 `INT_MAX`，轉型安全

分層的完整界定、三條邊界規則與移植時各層的命運見 `architecture.md`。

---

## 5. 模擬器與驅動的獨立性（最重要的規則）

模擬器（`sim/`）與驅動（`src/`）出自同一個對協定的理解，同一份誤解可能同時出現在兩邊、
互相吻合而不被發現。因此：

- **`sim/` 與 `src/` 不共用任何程式碼**，彼此不 include 對方的 header。
  兩邊唯一的共同點是 `include/sts_hal.h`
- 模擬器的 parser 刻意寫得簡單直白，不要「重用」驅動的 parser
- **驗收用的封包一律逐 byte 照抄 `protocol-notes.md` 第 4 節**，寫成常數陣列。
  **不可**用受測程式碼產生預期值
- 驗收順序：A（手冊 → 模擬器）→ B（驅動組包 vs 手冊）→ C（手冊回應 → 驅動解析）→
  D（驅動 ↔ 模擬器）。D 只證明兩邊彼此同意，不能取代 A/B/C
- RESET 範例：手冊印 `F6`，正確為 `F2`（已驗證，驗算腳本見 `../tools/verify.py`），
  測試以 `F2` 為準

---

## 6. Python 控制層（`pyctrl/`）

- **Python 3.12.16**（F-50）。第 5 步會 import lerobot，控制層必須跑在同一個環境
- **時間戳走 ctypes**：依 `interface-spec.md` §2.4 直接呼叫 `GetSystemTimePreciseAsFileTime()`。
  3.13 以前的 Windows 上 `time.time_ns()` 解析度只有約 15.6 ms，比要量的數毫秒迴路還粗
- **不用 numpy**。理由是學習目的：控制迴圈要能逐行說出在做什麼，`struct` 與 list 就夠
- 不用 asyncio、多執行緒、類別繼承、任何框架；單一同步迴圈
- 縮排 4 格

## 7. 量測與輔助工具（`tools/`）

- **可以用 numpy**，而且量測迴圈內應該用。理由與 §6 相反：工具的目的是**不干擾被量測的對象**，
  迴圈內不可配置記憶體，預配置 numpy 陣列是必要手段
- 量測迴圈內不寫檔、不配置記憶體、不做單位換算。有緩衝的檔案物件會週期性自動 flush，
  製造出等間隔尖峰，剛好偽裝成 OS 排程特徵
- 間隔一律用 monotonic 的 `perf_counter_ns()`；牆鐘只在開頭取一次當錨點
- 唯讀工具（如 `snapshot.py`）要有兩道防線：組包時限制指令碼、送出前再檢查一次

---

## 8. 檔案歸屬與命名

- **`datasheet/` 唯讀**。原廠規格書、協定手冊、記憶體表不編輯、不整理、不改名，
  保持拿到時的原樣。自己消化後寫的東西一律進 `docs/`

  理由：本專題至今抓到六處原廠文件的錯誤（F-02、F-15、F-16、F-17、F-22、F-26）。
  每一次都要能分辨「原廠是這樣寫的」和「我抄錯了」，兩邊混在一起就查不出來

- **新增檔案或資料夾時避開 `capstone`、`project`、`nycu` 這類字樣**。這個 repo 會跟著
  進履歷與面試，`sts3215-driver` 讀起來是一個驅動程式，`nycu-capstone-2026` 讀起來
  是一份作業。內容一樣，預期不同
