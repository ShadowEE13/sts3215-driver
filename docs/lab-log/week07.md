# 實驗記錄 — 工作記錄 07（學期第 9 週）

**學期週次**：W9（11/02–11/08），依 `schedule.md` 總表編號
**本篇期間**：2026/11/03（一）– 11/04（三）
**主題**：HAL 介面、虛擬馬達模擬器、Protocol 層純函式
**撰寫**：2026/11/04

> 檔名的 `week07` 為工作記錄流水號，與學期週次不同（期中考三週無記錄，流水號未前進）。報告引用請以學期週次 W9 為準，對照表見 `schedule.md`「週次編號慣例」。

---

## 一、本週摘要

本週完成驅動的軟體基礎：HAL 介面定案、虛擬 STS3215 模擬器、Protocol 層的組包與編解碼。**驗收 A（模擬器 vs 手冊）與驗收 B（驅動組包 vs 手冊）皆通過**，「協定理解正確」已有兩道獨立的外部證據。

另外以原廠記憶體表逐址核對模擬器，發現 Lock 暫存器的語意與直覺相反（F-15），並據此修正模擬器。這也說明了模擬器的每個假設都必須回到原始文件核對。

同日完成三份系統層級文件（系統通訊架構、上層介面規格、待確認事項），並在覆核時發現兩端時間戳的時鐘定義不一致，會使 W12 的延遲量測失效，已修正（C-16）。

需要真機的工作（單軸真機讀寫、參考封包擷取）安排在 W10 手臂可預約後進行（C-09）。

---

## 二、完成事項

**規劃與 repo 整理**
- 完成 `docs/week06-08-plan.md`：W6–W8 實作計畫，步驟 0–11 與驗收 A–D
- 原廠文件與論文移出 repo，並改寫 git 歷史；README 改為列出取得來源（C-11）
- 新增 `CLAUDE.md`：程式撰寫規範、HAL 決策、模擬器與驅動的獨立性規則、工作流程（C-13）

**系統層級文件**
- `docs/system-comm.md` v0.4：全系統 L1–L10 連線規格、方塊圖、控制週期時序、可量測區段
- `docs/interface-spec.md` v1.1：L3 上層介面封包格式，v1.0 凍結交付 B，v1.1 補時鐘規定
- `docs/open-questions.md`：16 項未決事項，分給 A、B 與三人共同決定
- `docs/memory-table-check.md`：原廠記憶體表逐址核對紀錄

**開發環境**
- Windows + MSYS2 UCRT64（gcc、make），git 共用 Git for Windows（C-12）
- `Makefile`：`make test`（含 demo）、`make demo`、`make layers`（分層檢查）、`make SAN=1`；處理 Windows 的 `.exe` 副檔名
- `tests/check.h`：自寫測試框架（`CHECK`、`CHECK_EQ`、`CHECK_BYTES`），失敗時逐 byte 標出差異

**步驟 1：HAL 介面 `include/sts_hal.h`**
- 四個函式指標 `write` / `read` / `flush_rx` / `millis`，第一個參數皆為 `void *ctx`
- 函式表 `hal_ops_t`（`static const`）與實例 `sts_hal_t`（`ops` + `ctx`）分開（C-14）
- 溢位安全的時間比較 `hal_timed_out()`
- `tests/test_hal.c`：計數器溢位前後的逾時判斷、兩條 bus 共用一張函式表且 `ctx` 互不干擾（26 項）

**步驟 2–4：虛擬 STS3215 模擬器 `sim/`**
- Control Table 128 bytes，逐址標註讀寫屬性，已與原廠記憶體表核對（`docs/memory-table-check.md`）
- 指令：PING / READ / WRITE / SYNC READ / SYNC WRITE、廣播 ID、應答級別 0x08
- Lock 語意（Lock = 1 寫入生效但不保存）、`sim_power_cycle()` 模擬斷電重開
- 運動模型：依速度朝目標前進、受角度限位、扭矩關閉不動、Phase BIT3 決定速度 0 的行為
- 故障注入：不回應、強制 error byte、錯誤 checksum、插入雜訊、SYNC 中途掉一顆
- `hal_sim`：虛擬時鐘（讀不到資料時前進 1 ms，逾時測試確定且瞬間完成）
- `sim/README.md`：能測 / 不能測、預設值來源、待真機驗證清單

**步驟 5：Protocol 層與 Device 層純函式**
- `src/sts_protocol.c`：`sts_checksum`、`sts_build_packet`
- `src/sts_device.c`：sign-magnitude 編解碼（超範圍截斷、`INT32_MIN` 安全）、little-endian 讀寫

**W10 準備（10/11 完成，尚未放入 repo）**
- `tools/snapshot.py`：LeRobot 執行前的唯讀暫存器快照與 READ 往返時間量測，程式層級保證唯讀
- `tools/fake_bus.py`：以虛擬序列埠扮演 6 顆馬達，在家測試 snapshot.py

---

## 三、驗收與測試結果

| 驗收 | 內容 | 結果 |
|---|---|---|
| A | 手冊範例 4.1–4.3 → 模擬器 → 回應與手冊逐 byte 相同 | ✅ 通過 |
| B | 驅動組出的 4.1–4.3 指令包與手冊逐 byte 相同；RESET checksum = `F2` | ✅ 通過 |
| C | 手冊回應包 → 驅動解析 | 未開始（步驟 6） |
| D | 驅動 ↔ 模擬器端對端 | 未開始（步驟 7–8） |

| 測試程式 | 項數 |
|---|---|
| `test_device`（sign-magnitude、little-endian） | 653 |
| `test_hal` | 26 |
| `test_protocol`（驗收 B、checksum） | 20 |
| `test_smoke` | 4 |
| `demo`（驗收 A、模擬器自我檢查） | 169 |
| **合計** | **872，全數通過** |

**驗證強度**
- gcc 與 clang 皆零警告（`-Wall -Wextra -Werror -Wconversion`；加嚴 `-Wsign-conversion -Wshadow -Wpedantic` 亦零警告）
- AddressSanitizer + UBSan 全數通過（於 Linux 執行，MSYS2 不支援）
- **反向驗證**：故意把 checksum 改成連 `FF FF` 一起加，驗收 B 有 4 項失敗，證明測試對這類錯誤有鑑別力
- **獨立對照**：另以 Python 依手冊公式重寫組包與 sign-magnitude，與 C 版結果比對一致（含 248 參數的最大封包、`INT32_MIN` 等極端值）

---

## 四、新發現

### 原廠記憶體表核對（詳見 `docs/memory-table-check.md`）

**F-15　Lock（0x37）控制的是「是否存進 flash」，不是「能否寫入」**
原廠表：寫 0 → 寫入 EPROM 的值掉電保存；寫 1 → 掉電不保存。Lock = 1 時寫 EPROM 仍立即生效，斷電後恢復；上電預設為 0。
影響：模擬器原本把 Lock = 1 實作成「拒絕寫入」，已修正。安全層可在 Lock = 1 下暫時調整角度限制，立即生效又不消耗 flash 寫入壽命。

**F-16　原廠記憶體表自相矛盾**
0x30 轉矩限制標為 2 bytes，0x31 卻另列為「無定義」。0x31 實為 0x30 的高位元組，模擬器維持可寫。

**F-17　Goal_Velocity = 0 的行為由 Phase（0x12）BIT3 決定**
BIT3 = 0 停止、BIT3 = 1 最高速。原廠表未給 Phase 初始值。
處置：驅動一律明確寫入速度，不依賴速度 0 的行為。

### SDK 與工具鏈（10/11 撰寫 snapshot.py 時，以及 11/03–04 實作時）

**F-18　LeRobot 使用的 SDK 與原廠 SDK 是兩套不相容的 API**
PyPI 的 `feetech-servo-sdk`：`PacketHandler(0)`、`readTxRx(port, id, addr, n)`；原廠 GitHub `FTServo_Python`：`sms_sts(port)`、`readTxRx(id, addr, n)`。函式同名、參數不同。原廠修正逾時 bug 的版本未發布到 PyPI，LeRobot 因此以 monkeypatch 修補。

**F-19　SDK 的 `ping()` 實際送出兩包**
PING 之後再送一包 READ 0x03 讀型號。以 `ping()` 量測往返時間會量到兩次往返。

**F-20　SDK 的 parser 會把 echo 當成合法回應**
送出的 PING `FF FF 01 02 01 FB` 若被轉接板回送，字頭、ID、Length、checksum 全部合法，Instruction 0x01 會被當成 error byte（電壓異常）。光看 bytes 無法分辨。
處置：HAL 合約規定 echo 由 HAL 濾除（`sts_hal.h` 的 `read` 註解）。

**F-21　gcc 與 clang 的 `-Wconversion` 檢查範圍不同**
`uint16_t v = lo | (hi << 8);` clang 報錯、gcc 不報。little-endian 組合正是這種寫法，需兩種編譯器都編過。

### 文件覆核

**F-22　兩端時間戳的時鐘定義不一致，會使延遲量測失效**
`interface-spec.md` v1.0 的 Python 範例用牆鐘時間（Unix epoch），但 `system-comm.md` v0.3 寫控制層以 `QueryPerformanceCounter` 取時間。QPC 的起點是開機後的任意時刻，兩者相減得到的是時鐘原點差，不是延遲。

連帶發現第二個問題：Windows 上 `time.time()` 在部分 CPython 版本的實際解析度僅約 15.6 ms（系統計時器節拍），比待量的數毫秒控制迴路還粗，量測誤差會大於被量對象。

處置見 C-16。此項不是硬體或 SDK 的問題，而是自己兩份文件之間的不一致——若未在實作前發現，會在 W12 才以「數字看不懂」的形式浮現。

---

## 五、待驗證清單（新增）

| 編號 | 項目 | 來源 | 優先 |
|---|---|---|---|
| V-13 | Lock = 1 時寫 EPROM 是否立即生效、斷電後是否恢復 | F-15 | 高 |
| V-14 | 實際 Phase（0x12）值，特別是 BIT3（速度 0）與 BIT4（單圈/多圈）；併入 V-08 | F-17 | 中 |
| V-15 | 0x47 以後（原廠表未列）的行為；LeRobot 會寫 0x50–0x55 | 記憶體表 | 低 |
| V-16 | Windows 序列埠 `write` 回傳時，資料是否已真的送上線（HAL 合約決定 2） | HAL | 中 |
| V-17 | 斷電重開後：位置是否保留、0x41 狀態是否清為 0、扭矩是否預設關閉 | 模擬器假設 | 低 |
| V-18 | `GetSystemTimePreciseAsFileTime()` 與 Python `time.time_ns()` 在同一台機器上的實際一致性與解析度 | F-22 | 中 |

模擬器其餘自行決定的行為（扭矩關閉時寫目標位置、寫唯讀位址、改 ID 後的應答等）列於 `sim/README.md`「待真機驗證」，W10 逐條確認。V-06（echo）因 F-20 優先度提高為**高**。

---

## 六、計畫變更

**C-07　優先級調整：團隊保底線與個人交付物分開（10/11）**
與教授及學長討論後：三人應優先讓各自的部分能運作，下學期才對接。學長未否定 C 驅動的價值，只是順位問題。
做法：介面凍結。對隊友 A、B 公開固定的上層介面，第一版後端用 LeRobot（團隊保底），第二版換成自製 C 驅動（本子題目成果），對隊友無感。

**C-08　STM32 移至下學期（11/03）**
W13 判斷點與 W14–16 移植階段取消，STM32 改為個人 side project。A/B/C 對照實驗中，C 組（STM32 獨立控制）移至下學期。

**C-09　真機相關工作集中至 W10（11/03）**
手臂自 W10（11/09 當週）起可預約。單軸真機讀寫與參考封包擷取集中在 W10 的實驗室場次完成；不需硬體的部分以模擬器先行。

**C-10　上層介面傳輸方式定為 localhost UDP（11/03）**
控制資料會過期，掉包時要的是下一包而非重傳；TCP 的 head-of-line blocking 對控制流是錯的行為。

**C-11　原廠文件與論文移出公開 repo（11/04）**
受著作權保護的 PDF 不公開散布，並改寫 git 歷史徹底移除；本地保留於 `.gitignore` 列出的 `datasheet/`、`literature/`，README 改列取得來源。

**C-12　開發環境定為 MSYS2 UCRT64；PC 端 HAL 改用 Win32 API（11/04）**
理由：編出 Windows 原生執行檔，W10 的 PC 端 HAL（`hal_win32.c`，`CreateFile` / `ReadFile`）可在同一環境開發。不選 WSL：存取 USB 序列埠需多一層 `usbipd`。
代價：MSYS2 無 sanitizer，改由 review 時於 Linux 補跑。

**C-13　程式碼由 Claude Code 撰寫，本人負責理解、設計決策與審查（11/04）**
另一個 Claude 對話負責 review、解釋與決策紀錄；每步完成後確認理解。之後有時間再自行重寫核心部分（優先：收包狀態機）。
風險：模擬器與驅動出自同一模型，原本「不同作者」的保險消失。
因應：驗收 A/B/C 一律以手冊為外部參照、預期值照抄手冊；`sim/` 與 `src/` 不共用程式碼；反向驗證與 Python 獨立對照；原廠記憶體表逐址核對。

**C-14　HAL 函式表與實例分開（11/04）**
`hal_ops_t` 為 `static const` 函式表，`sts_hal_t` = `ops` + `ctx`。leader 與 follower 共用一張表、各帶 `ctx`；STM32 上 `const` 表放 flash 不佔 RAM。即 Linux kernel `struct file_operations` 的模式。

**C-15　HAL 不加 µs 計時（11/04）**
W12 延遲量測在應用層取時間；端到端延遲用上層 UDP 封包的 timestamp 欄位。避免為了量測而讓三種 HAL 實作都多一個函式。

**C-16　時間戳統一為 Unix epoch 牆鐘微秒（11/04）**
起因 F-22。規定：B 的 Python 橋接用 `time.time_ns() // 1000`，控制層用 `GetSystemTimePreciseAsFileTime()` 換算（減 `116444736000000000`、除 10）。**禁用** `QueryPerformanceCounter` 與 `time.perf_counter`——其起點為開機或行程啟動後的任意時刻，跨行程相減無意義；亦不用 `time.time()`，解析度不足。
規定寫入 `interface-spec.md` §2.4；`system-comm.md` §3.2 同步修正。封包格式未變，B 已開始的實作不需重寫。
C-15 仍成立：純粹在控制層內部、不跨行程的區段（例如單包 UART 往返）可用高解析度計時器。

**C-17　明確區分本學期可量測與不可量測的延遲區段（11/04）**
本學期可量：控制迴路延遲 `t1 − t0`（橋接蓋章 → 控制層 → 手臂 → 回應包）。
本學期不可量：完整端到端 `t2 − t0`（加上實體運動、攝影機、串流、Unity、頭盔顯示），需 L10 與影像鏈，皆在下學期。
報告與 meeting 須明確說明量的是哪一段。區段定義見 `system-comm.md` §3.1。

**C-18　週次編號慣例（11/04）**
報告與 `schedule.md` 一律用學期週次 W3–W16；`lab-log/` 檔名為工作記錄流水號，兩者不一致。每份記錄標頭須同時寫出學期週次與日期，對照表置於 `schedule.md`。

---

## 七、檢查點達成

| 檢查點 | 結果 |
|---|---|
| HAL 介面定案 | **達成** |
| 模擬器 + 驗收 A | **達成** |
| Protocol 層組包 + 驗收 B | **達成** |
| 系統通訊架構與上層介面文件 | **達成** |
| 收包狀態機 + 驗收 C | 未開始（步驟 6） |
| 單軸 READ / WRITE 打模擬器（驗收 D） | 未開始（步驟 7–8） |
| 單軸真機讀寫 | 移至 W10（需真機，C-09） |

---

## 八、下一步

**在家（11/04–11/08）**
- 步驟 6：收包狀態機 + 驗收 C（含 echo 濾除，F-20）
- 步驟 7：單軸 API + 驗收 D
- 步驟 8–11：逾時重傳、錯誤碼解析、故障注入的驅動側測試、SYNC READ / WRITE
- 將 `interface-spec.md` **v1.1** 發給 B（v1.0 已發者需告知 §2.4 的時鐘規定，否則 W12 量不到延遲）；`system-comm.md` v0.4 發給 A、B
- `tools/snapshot.py`、`tools/fake_bus.py` 放進 repo

**W10 實驗室**
- 依序：snapshot.py（最先，取出廠值）→ 跑通 LeRobot → 擷取參考封包
- 硬性檢查點：自算 checksum 與真機封包逐 byte 一致
- 實作 `hal_win32.c`，驅動接上真機：先單軸、再六軸
- 逐條確認 V-01 – V-18 與 `sim/README.md` 的待驗證清單

---

## 附：meeting 簡報素材

距上次正式報告（10/06，W5）已四週，下次 meeting 的素材：

1. **驗收 A、B 的終端輸出**：手冊上的 bytes vs 模擬器 / 驅動產生的 bytes（`make demo > demo-output.txt`）
2. **F-15 Lock 的故事**：依筆記寫的模擬器是錯的，核對原廠表才發現；說明為何每個假設都要回原始文件
3. **測試方法論**：872 項測試、反向驗證、Python 獨立對照、sanitizer，回應「模擬器與驅動同源」的質疑
4. **HAL 設計**：函式表 + `ctx` 的模式、49.7 天溢位陷阱與 `hal_timed_out()`
5. **AI 協作的分工與防範**（C-13）：主動說明，而非被問才解釋
6. **系統通訊架構圖**：L1–L10 全系統分工，以及 C 的邊界在哪
7. **F-22 時鐘不一致**：自己兩份文件的矛盾，在實作前發現；說明文件覆核與實作同等重要
8. **LeRobot 基準環境已就緒**（詳見 `week06.md`）：1 頁背景，模擬 vs 實體手臂的對應表，最後一列連到 C 驅動的 SYNC READ / SYNC WRITE
