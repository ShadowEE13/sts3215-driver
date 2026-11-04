# 實驗記錄 — 工作記錄 06（學期第 6 週）

**學期週次**：W6（10/12–10/18），依 `schedule.md` 總表編號
**主題**：LeRobot 模擬環境建置（PushT + 官方預訓練 diffusion policy）
**撰寫**：2026/11/04（補記）

> 本篇在 `week07` 之後才補記，F / V / C 編號接續 `week07`（F-23、V-19 起），因此編號比 `week07` 大。編號規則不變：跨週連續、不重置。

---

## 一、本週摘要

期中考前，在自己的 Windows 筆電建好 LeRobot 環境，載入官方預訓練的 diffusion policy，在 PushT 模擬環境中評估 10 回合，成功 7 回合。

目的是不碰硬體，先確認整條工具鏈（Python、CUDA、LeRobot、模擬器）可用，並看懂 LeRobot 的控制循環：observation → policy → action。

與專題的關係：依 C-07，團隊保底的第一版後端是 LeRobot。W10「跑通 LeRobot」這一步的軟體安裝已提前完成，到實驗室後可以直接從接上手臂開始，省下稀缺的預約時段。

原訂 W6 的驅動工作（HAL、模擬器、Protocol 層）本週未執行，已於 W9 補做（見 `week07`）。

---

## 二、完成事項

**環境**
- 安裝 Miniforge（conda），建立 Python 3.12 環境 `lerobot`，安裝 ffmpeg
- LeRobot 原始碼 clone 至 `C:\Users\eason\projects\lerobot`，以 editable 模式安裝，含 PushT 模擬器、評估工具與 `[diffusion]` extra
- PyTorch 換成 CUDA 版，模型以 GPU 執行
- LeRobot 用 Miniforge Prompt / PowerShell，C 驅動用 MSYS2 UCRT64（C-12），兩套環境各自獨立

**模型與評估**
- 將 `lerobot/diffusion_pusht` 下載至本機，以官方腳本轉換成新版 processor 格式
- `lerobot-eval` 跑 10 回合，成功 7 回合；影片存於 `outputs\eval\`

**待補**
- 本機 LeRobot 的 commit hash（見 V-19）
- eval log 中的 `avg_max_reward`（平均最大重疊率），可與官方數字對照

---

## 三、結果與解讀

| 項目 | 本機 | 官方模型卡 |
|---|---|---|
| 評估回合數 | 10 | 500 |
| 成功率 | 70%（7/10） | 65.4% |
| 平均最大重疊率 | 待補 | 0.955 |

成功的定義：該回合中方塊與目標的最大重疊率達 95% 以上。

**能下的結論：環境與模型載入正確。** 若權重、正規化或 processor 轉換有錯，成功率會遠低於此。

**不能下的結論：「與官方結果一致」。** 10 回合的統計量太小：

- 7/10 的 95% 區間約為 39%–89%
- 若真實成功率就是 65.4%，跑 10 回合時，成功次數落在 4–9 次的機率約 96%。也就是說，4/10 或 9/10 同樣「符合官方」
- 10 回合分不出 50% 和 80%

**報告用語**：寫「環境可正常執行官方模型（10 回合成功 7 次；官方 500 回合為 65.4%）」，不寫「驗證結果與官方一致」。
若之後真的需要宣稱一致：100 回合約 ±9%，500 回合約 ±4%。對本專題而言非必要。

---

## 四、遇到的問題（環境建置）

| # | 錯誤訊息 | 原因 | 解法 |
|---|---|---|---|
| 1 | `conda is not recognized` | 電腦沒裝 conda | 安裝 Miniforge，用 Miniforge Prompt 或執行 `conda init powershell` |
| 2 | `Permission denied`（git clone） | 當下停在沒有寫入權限的系統資料夾 | `cd ~` 回到使用者資料夾 |
| 3 | numpy / fsspec 版本衝突 | 重裝 CUDA 版 torch 時用了 `--force-reinstall`，連帶把這兩個套件升得太新 | 指定版本，降回相容範圍 |
| 4 | `cannot find context for 'forkserver'` | 預設平行跑多個模擬環境，此方式 Windows 不支援 | 加 `--eval.use_async_envs=false` |
| 5 | `'diffusers' is required` | 安裝時漏了 `[diffusion]` extra | `pip install -e ".[diffusion]"` |
| 6 | `HFValidationError: lerobot\diffusion_pusht` | Windows 把 `/` 轉成 `\`，變成不合法的 repo 名稱 | 先用 `snapshot_download` 下載到本機，改傳本機路徑 |
| 7 | `requires migration to processor format` | 模型為舊版格式；新版 LeRobot 把正規化拆成獨立的 processor 檔 | 執行官方轉換腳本 |
| 8 | `Couldn't encode 84`，`config.json` 為空 | 轉換腳本儲存 config 時有 bug | 從原資料夾複製 `config.json` 與 `model.safetensors` |

**分類**
- 安裝流程：1、2、3、5
- Windows 相容性：4、6（→ F-23）
- 新舊版本不相容：7、8（→ F-24）

之後遇到錯誤，先判斷屬於哪一類，比較快找到方向。

**預防**：#3 的根源是 `--force-reinstall` 會連依賴一起重裝。之後換 torch 版本時不加此參數，或加 `--no-deps`；裝完執行 `pip check` 確認沒有衝突。

---

## 五、新發現

**F-23　LeRobot 在 Windows 上的兩個相容性問題**
(a) 評估時預設以多行程平行跑模擬環境，使用的 `forkserver` 啟動方式 Windows 不支援，需關閉非同步環境。
(b) 參數中的 Hub repo id 被當成路徑處理，`/` 被轉成 `\`。
影響：W10 會在同一台 Windows 筆電上跑實體手臂流程（find-port、calibrate、teleoperate、record），同類問題（多行程、路徑）可能再出現（→ V-20）。遇到時先對照這兩項。

**F-24　新版 LeRobot 把正規化從模型拆出，存成獨立的 processor 檔**
Hub 上的舊版模型要先轉換才能載入，且官方轉換腳本會寫出空的 `config.json`，需手動補回。
意義：一個可用的模型 = 權重 + config + 正規化統計值。統計值來自訓練資料，所以：
- 用自己錄的資料訓練時，統計值從自己的資料計算
- 換了手臂或相機配置，舊模型不能直接沿用

**F-25　Diffusion policy 一次推論、分 8 步執行（action chunking）**
log 中的設定：`horizon: 16`、`n_action_steps: 8`（`n_obs_steps` 應為 2，待核對 log）。每次推論輸入最近 2 步的觀測，預測 16 步動作，從「目前這一步」起執行 8 步；動作佇列清空後才再推論一次。
精確地說，執行的是 16 步中的第 1–8 步（index 從 `n_obs_steps − 1` 起算），第 0 步對應上一個觀測，不執行。此細節依 LeRobot 原始碼 `modeling_diffusion.py`，待以本機版本核對。
意義：推論只發生在佇列清空的那一步，因此那一步的耗時遠大於其餘 7 步，控制週期不均勻。上實體手臂時，fps 與推論延遲的取捨就從這裡來。（推論，未實測）

---

## 六、模擬 vs 實體手臂的對應

| | PushT 模擬 | SO-101 實體手臂 |
|---|---|---|
| observation | 影像 + 圓點的 2D 位置 | 相機影像 + 6 顆馬達的目前位置（5 關節 + 夾爪） |
| action | 圓點的 2D 目標位置 | 6 顆馬達的目標位置 |
| 每一步在做什麼 | 模擬器 step | bus 上一次 SYNC READ（0x38）+ 一次 SYNC WRITE（0x2A） |

log 中的 `input_features` / `output_features` 之後就會對應到手臂的關節數與相機數。

**與 C 驅動的連結**：實體手臂上，LeRobot 每個控制步在 bus 上做的事，就是 C 驅動步驟 11（SYNC READ / SYNC WRITE）要實作的東西。W10 擷取的參考封包，就是這兩個指令在真機上的樣子。

**與數位孿生的連結**：模擬環境本身就是一個虛擬的機器人；數位孿生要做的是讓虛擬與實體同步。每個控制步讀到的關節狀態，就是下學期串到 Unity / VR 的資料來源（L10 手臂狀態回傳）。

---

## 七、待驗證（新增）

| 編號 | 項目 | 來源 | 優先 |
|---|---|---|---|
| V-19 | 本機 LeRobot 的 commit 與 `LeRobot-notes.md` 引用的 `8c920c4`（10/03）是否相同；若較新，F-06（`_sync_read` 不檢查 error byte）與 F-18（monkeypatch）需在本機版本上複查 | 版本一致性 | 中（在家可做） |
| V-20 | 實體手臂流程（find-port / calibrate / teleoperate / record）在 Windows 上是否遇到 F-23 同類問題 | F-23 | 中（需真機） |

---

## 八、計畫與建議

本週無計畫變更（C 編號不增加）。

**Ubuntu 雙系統（建置過程中出現的建議）**：暫不採用。C 驅動的 PC 端 HAL 本來就是 Win32（`hal_win32.c`，C-12），LeRobot 在本專題只是團隊保底與對照基準，本週遇到的 Windows 問題都已有解法。若 W10 之後錄資料或訓練在 Windows 上反覆卡住，再評估（例如只把訓練放到 Linux）。

---

## 九、檢查點

| 檢查點 | 結果 |
|---|---|
| 原訂 W6：模擬器、HAL、Protocol 層（依 `week05` 第八節） | **未執行** — 期中考，W9 補做（`week07`） |
| 原訂：跑通官方 LeRobot 範例（需真機） | **未執行** — 手臂 W10 才可借；改以 PushT 模擬完成軟體端驗證 |
| （本週新增）LeRobot 環境在 Windows 上可用 | **達成** |

---

## 十、對 W10 的準備（在家先做）

- [ ] 在同一個 `lerobot` 環境安裝馬達驅動：`pip install -e ".[feetech]"`，確認可正常 import，實驗室不必再裝
- [ ] 記下 commit（V-19）：`git -C C:\Users\eason\projects\lerobot rev-parse --short HEAD`
- [ ] W10 順序不變：**`tools/snapshot.py` 最先**，LeRobot 之後。環境已經裝好，更要避免到場就先跑 calibrate——calibrate 會寫入暫存器，出廠值就取不回了

---

## 附：meeting 簡報素材

這週的內容適合放 **1 頁**，作為「團隊保底後端已就緒」的背景，不是主打（主打仍是 W9 的驗收 A/B、F-15、F-22）：

- 標題：LeRobot 基準環境（Windows）已就緒
- 第六節的對應表：模擬 vs 實體手臂，最後一列連到 C 驅動的 SYNC READ / SYNC WRITE
- 數字寫法：10 回合成功 7 次；官方 500 回合為 65.4%；明講這只證明環境可用
- 一句話帶過 Windows 坑的三分類

已併入 `week07` 的 meeting 素材清單（第 8 項）。
