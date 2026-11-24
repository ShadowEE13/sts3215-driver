# 實驗記錄 — 學期第 11 週

**學期週次**：W11（11/16–11/22），依 `schedule.md` 總表編號
**本篇期間**：2026/11/16（一）– 11/22（日）
**主題**：組裝前準備、Python 第 1 步、11/19 組裝場次與出廠快照
**撰寫**：2026/11/24（補記）

> F / V / C 編號接續 `week10.md`：F-30～F-45、V-24～V-28 與 V-32、C-31～C-33。
> 本篇在 W12 已進行數日後才補記，因此有兩處**後見之明的註記**，以 → 標出：W12 的發現修正了本週的部分結論。本週當時的判斷保留原樣，依 C-23 不回頭改寫。
> V-32 的號碼大於 V-31（W12 配號），因為問題發生在 W11 但到 11/24 才正式編號。編號規則不變：跨週連續、不重置。

---

## 一、本週摘要

本週分三段：出發前準備、11/19 的組裝場次、以及回家後的資料比對。

**出發前（11/16–11/18）**：寫組裝檢查清單、重寫 snapshot 工具、在 `lerobot` 環境裝馬達套件、完成 Python 第 1 步。其中 snapshot 工具是重寫而非沿用——10/11 寫的版本從未 commit，組裝前才發現已遺失（C-32）。

**組裝場次（11/19 晚）**：用 FD 設完 12 顆馬達 ID 後、機構組裝之前，取得 leader 與 follower 各 6 顆的**出廠暫存器快照**。這是整個專題中只有一次機會的資料——calibrate 與 connect 都會改寫暫存器。follower 完成組裝與 calibrate；leader 尚未組裝。

**回家比對（11/20）**：用一張「連 ID 都沒改過」的 FD 截圖與 snapshot 逐址對照，確認 snapshot 等同出廠狀態，並整理出依型號分族的出廠暫存器表（F-44）。

**本週最重要的發現是負面的**：文獻推算的兩個延遲瓶頸，在本專題的硬體上**都不存在**（F-36、F-37）。這直接改變了 W13 延遲量測的方向。

---

## 二、出發前準備（11/16–11/18）

| 日期 | 項目 | 產出 | commit |
|---|---|---|---|
| 11/17 | 組裝檢查清單 | `docs/assembly-checklist.md` | `b8b01bc` |
| 11/17 | 重寫 snapshot 工具 | `tools/snapshot.py`、`tools/fake_bus.py`，自我測試 41 項 | `f6705fc` |
| 11/17 | `lerobot` 環境裝馬達套件 | `pip install -e ".[feetech]"`；`pip show rustypot`、`pip check`、`lerobot-setup-motors --help` 皆通過 | — |
| 11/17 | Python 第 1 步 | `pyctrl/receiver.py`、`pyctrl/sender_demo.py` | `c4bc5c2` |
| 11/17 | 學習筆記 01 | `docs/learning/python-01-udp-socket.md` | `93cef40` |

---

## 三、11/19 組裝場次

### 實際流程

1. 用 **FD**（飛特官方除錯軟體）設馬達 ID，不是 `lerobot-setup-motors`
2. ★ **snapshot**：leader、follower 各跑一次，12 顆全部回應，無逾時、無錯誤
   - `snapshots/20261119_193036_leader-preassembly.json`（COM10，5V）
   - `snapshots/20261119_193607_follower-preassembly.json`（COM10，12V）
3. Follower 組裝 → `lerobot-calibrate`（本機 LeRobot `ba83e4cf`，id `my_follower`，COM14）
4. **Leader 尚未組裝**，改天再裝。出廠值已在組裝前存下，不受影響

第 2 步是本週的關鍵。依 C-31 調整過時機後，它落在「ID 設定完成、機構組裝之前」——六顆已串好、ID 為 1–6，而 calibrate 尚未執行，暫存器仍是出廠值。

### Follower calibrate 結果

| 關節 | MIN | MAX | 約略角度 |
|---|---:|---:|---|
| shoulder_pan | 787 | 3243 | 216° |
| shoulder_lift | 852 | 3221 | 208° |
| elbow_flex | 852 | 3065 | 195° |
| wrist_flex | 908 | 3253 | 206° |
| gripper | 2047 | 3530 | 130° |

- 新版提示移動 `shoulder_pan, shoulder_lift, elbow_flex, wrist_flex, gripper`；`wrist_roll` 不錄範圍（全圈馬達）
- 所有範圍都遠離 0 / 4095，避開溢位
- ⚠️ **gripper 的 MIN 剛好等於 2047（中點）**：若按中間姿勢時夾爪為全閉則無問題；若為半開，代表範圍沒錄到閉合端，需重做 calibrate。列為 **V-32**
- 校正檔：`C:\Users\eason\.cache\huggingface\lerobot\calibration\robots\so_follower\my_follower.json`

### 實驗室既有做法（學姊 10/7 簡報 `so101.pptx`）

- FD 圖形介面：把關節轉到極限、觀察數值，填入 Min / Max Position Limit，留 ±10～20 餘量
- 指令：`lerobot-calibrate`、`lerobot-teleoperate`、`lerobot-record`、`lerobot-replay`
- 本次判斷：上下限以 homing offset 為基準，**要先 calibrate 再用 FD 設上下限**，順序反過來會對不上

---

## 四、FD 截圖 × snapshot 對照（11/20）

**資料**：follower 第一顆馬達在 FD 改 ID **之前**的記憶體表截圖（11/19 19:16，畫面上只有一顆、ID 1），存於 `snapshots/fd/`。這是 12 顆中唯一「連 ID 都沒動過」的樣本，用來驗證 snapshot 是否真的等同出廠狀態。

結論見 F-42～F-45。

---

## 五、新發現

### 5.1 LeRobot 與 SDK

**F-30　本機 LeRobot 已不使用 `scservo_sdk`，改用 `rustypot`**
本機版本 `ba83e4cf`（2026-11-03）；`LeRobot-notes.md` 讀的是 `8c920c4`（10/03）→ **V-19 有答案：不同版本**。
`pyproject.toml` 的 `feetech = ["lerobot[serial-motors]"]`，`serial-motors` = `rustypot>=1.13.1` + `pyserial`（註解說明 pyserial 只供 `lerobot-find-port` 使用）；`motors_bus.py` 直接 `import rustypot`，全專案已無 `scservo_sdk`。
影響：**F-18、F-19、F-20 降級**為「10/03 版本的觀察」，不再描述 LeRobot 現況；`LeRobot-notes.md` §2 的呼叫鏈（PacketHandler / PortHandler）過時，現為 `rustypot.Bus`；報告標註版本時要用實際執行 setup/calibrate 那台電腦的版本。
意義：`snapshot.py` 不依賴 SDK，後端整個換掉也不受影響。與 C-07 介面凍結同一個道理——**依賴越少，上游變動的波及範圍越小**。

**F-31　新版 LeRobot 會讀 error byte，但警報不拋例外**
`read()` / `sync_read()` 透過 `read_register_with_error` 取得 status；馬達**拒絕**指令時拋 `RuntimeError`，但 docstring 明寫**過載等警報不會拋例外**。`max_relative_target` 預設仍為 `None`（`config_so_follower.py:36`）。
影響：F-06（「`_sync_read` 不檢查 error byte」）在新版不成立。安全層的論述從「它沒檢查」改為「**它讀到了警報但不處理，交給上層決定**」——這其實更精準，處理警報本來就是應用層的職責。

**F-32　SO-101 leader 使用三種減速比的馬達**
J1、J3 為 C044（1:191）；J2 為 C001（1:345）；J4–J6 為 C046（1:147）。Follower 六顆皆 1:345。
7.4V 的 STS3215 搭配 **5V** 電源，12V 馬達搭配 12V（來源：Seeed Studio SO-ARM101 Ch.6）。
影響：`schedule.md` 風險表寫的「7.4V leader / 12V follower」是**馬達額定電壓，不是變壓器電壓**，現場以實際標示為準（V-26，已由 F-39 回答）。

### 5.2 Python 與 Windows（第 1 步實作時）

**F-33　Windows 上 UDP 封包大於 `recvfrom` 緩衝區 → `WinError 10040`，超出部分丟棄**
實驗：送 10 bytes、`recvfrom(4)` → `WSAEMSGSIZE`。Linux 則靜默截斷。
影響：bufsize 用規格上限 512；**第 4 步安全層要接住 `OSError` 並丟掉那一包**，不能讓 B 送來一個壞封包就讓控制層停掉。

**F-34　Windows 上阻塞中的 `recvfrom` 按 Ctrl+C 無反應**
處置：`settimeout(1)`，逾時回到迴圈開頭。

**F-35　`sleep(0.01)` 的實際週期是 10.833 ms**
來源：Python 第 1 步驗收時，相鄰兩包 `timestamp_us` 的差。
影響：固定 `sleep` 會累積誤差（100 Hz 實際約 92 Hz）。這是第 3 步改以 `perf_counter()` 計算下一個喚醒點的數據依據。

### 5.3 組裝場次實測

**F-36　全新套件馬達的 `0x07` 出廠即為 0，不是規格書的 250（500 µs）**
12 顆全新拆封皆為 0；homing offset 與角度限制顯示尚未被 LeRobot calibrate 過。推測飛特出貨給 SO-101 套件時已預先設定。
影響：「維持出廠 500 µs → 六顆佔 3 ms、頻率掉到 260 Hz」的推算，對**泛用** STS3215 成立，對本套件不成立。報告要分開陳述。

**F-37　轉接板晶片為 WCH CH343P，不是 FTDI**
照片確認（板上標 TTL Model，USB-C）。READ RTT 中位數：leader 0.34–0.39 ms、follower 0.39–0.63 ms，p95 ≤ 0.96 ms。**echo：無** → V-06 有答案，F-20 在本硬體上不會發生。
影響：文獻的 FTDI latency timer 16 ms 瓶頸不存在。**加上 F-36，文獻推算的兩個延遲瓶頸在本硬體上都不存在** → W13 延遲量測從「驗證瓶頸」改為「確認瓶頸不在、量剩下的延遲來自哪裡」；latency timer A/B 對照需重新規劃（除非另找 FTDI 轉接板當對照組）。

**F-38　Phase（`0x12`）依減速比不同；型號碼無法區分減速比**
1:345（leader J2 的 C001、follower 全部）為 **12**（`0x0C`，BIT2、BIT3）；1:191 / 1:147（leader 的 C044、C046）為 **92**（`0x5C`，BIT2、BIT3、BIT4、BIT6）。
兩者 BIT3 皆為 1 → Goal_Velocity = 0 時為**全速**（F-17）。模擬器假設 `0x12` = 0 是錯的，要修。
12 顆型號碼皆 777、韌體皆 3.10 → V-25 有答案。
→ **W12 修正**：F-51 指出這個說法太強。C001 可由 Phase = 12、`0x0E` = 80、`0x18` = 16 辨識；真正分不出來的是 C044 與 C046（所有非即時暫存器逐 byte 相同）。需要標籤的是不要把 J1/J3 跟 J4–J6 裝反。
→ **W12 補充**：Phase 12 的 BIT4 = 0（單圈）、92 的 BIT4 = 1（多圈）。依 `LeRobot-notes.md` 4.4，LeRobot 會清 BIT4 強制單圈，所以 leader 的 C044/C046 四顆在 LeRobot 動到之前，直接讀位置可能不是 0–4095。見 `verification-list.md` 的 V-08 註記。

**F-39　電壓門檻依型號不同**
Follower：Max 14.0 V / Min 4.0 V。Leader：C001 為 8.0 / 4.0，C044、C046 為 12.0 / 4.0。
Leader 實際用 **5V** 變壓器（讀到 4.9–5.0 V），follower 用 12V（11.8–12.0 V）→ **V-26 有答案**。`schedule.md` 風險表的「7.4V leader」應理解為「7.4V 額定馬達、5V 供電」。

**F-40　Follower 出廠 homing offset 為 85，leader 為 0；組裝前位置讀數卡在 0 / 4095 交界**
位置讀數為 4092–4095、2、3。
這就是學姊簡報第 9 頁「初始姿勢，避免溢位」的原因：calibrate 用中間姿勢把中點移到 2047 附近。

**F-41　位址 `0x50–0x56` 的名稱（V-15）**
依實驗室簡報的 FD 截圖：80 Moving Threshold、81 DTs(ms)、82 Vk(ms)、83 Vmin、84 Vmax、85 Amax、86 KAcc（FD 標為 DEFAULT 區）。snapshot 多讀的 `0x47–0x56` 正好涵蓋，出廠值已在 JSON 裡。

### 5.4 FD 截圖對照（11/20）

**F-42　FD 改 ID 只動了 ID；C-31 的時機判斷得到驗證**
6 顆 follower 的 snapshot 與改 ID 前的 FD 截圖逐址比對，唯一的差異是 ID（預期）與 Present Position／Voltage／Temperature（即時讀值）。
位址 3 看似不同，其實是表示法差異：snapshot 讀成 2 bytes 的型號碼 777 = `0x0309`，FD 把它拆成兩個 byte、標為「Servo Main Version 9 / Sub Version 3」。
結論：**設完 ID 之後的 snapshot 等同出廠狀態（ID 除外）**。C-31 把時機從「組好後第一件事」改到「設完 ID、組裝之前」是對的。

**F-43　上電時 Lock（`0x37`）為 1，與原廠表不符**
FD 截圖（改 ID 前）與唯讀 snapshot 都讀到 **1**；`memory-table-check.md` 依原廠表寫上電預設為 **0**，模擬器原本也照 0 實作。
待確認：FD 在連線時是否會寫入 Lock（可能性低）。與 **V-13** 一併驗證。
→ **W12 處置**：模擬器的上電預設已改為 1（commit `5236354`），但 V-13 仍未驗證，`sim/README.md` 標為待確認。

**F-44　出廠暫存器依型號分兩大族（V-24 大致有答案）**

| 位址 | 名稱（FD） | Follower（C018） | Leader C001（J2） | Leader C044 / C046（J1、J3–J6） |
|---|---|---:|---:|---:|
| 0x0D | Max Temperature | 70 | 70 | **80** |
| 0x0E | Max Input Voltage | 140（14.0 V） | **80（8.0 V）** | **120（12.0 V）** |
| 0x12 | Setting Byte（Phase） | 12 | 12 | **92** |
| 0x18 | Punch | 16 | 16 | **0** |
| 0x1C | Overload Current | 310 | **500** | **300** |
| 0x1F | Position Offset | **85** | 0 | 0 |
| 0x26 | Overcurrent Protection Time | 200 | 200 | **250** |
| 0x51 | DTs(ms) | 20 | 20 | **10** |
| 0x54 | Vmax | 65 | **68** | **250** |
| 0x55 | Amax | 50 | 50 | **250** |
| 0x56 | KAcc | 1 | 1 | **100** |

其餘位址（含 `0x07` = 0、PID 的 P 32 / D 32 / I 0、Lock = 1、角度限制 0–4095）三者相同。1:345 的兩種馬達（C018、C001）設定接近；1:191 / 1:147 的 leader 馬達是另一套。
待做：整理成 `docs/factory-registers.md`。

**F-45　FD 把 `0x07` 標為「Reserved」**
對照 `protocol-notes.md`：xlsx 註明「STS 無此功能」，LeRobot 卻把它當 Return_Delay_Time 寫成 0。加上 F-36（全新馬達讀到 0）：**STS3215 上 `0x07` 可能根本沒有回應延遲的功能**，「500 µs 瓶頸」的前提可能來自 SMS/SCS 系列的文件。
新的驗證方式：把 `0x07` 寫成 250、量 RTT 是否增加約 0.5 ms、再寫回 0。可逆、低風險，排進 W13。
> 素材原寫「V-05 改寫」，是筆誤。`0x07` 那一項是 **V-01**；V-05 是「SYNC READ 中途一顆無回應」。`week10.md:163` 有同一個筆誤，依 C-23 不回頭改寫，本條為其更正。

**其他交叉驗證**
- **V-15**：snapshot 的 `0x47–0x56` 原始 bytes 與 FD 的 80–86 完全吻合；`0x4E–0x4F` 讀到 `FF FF`（FD 不顯示，用途未知）
- **LeRobot 會改的值**：P 係數出廠 32 → LeRobot 設 16（calibrate 輸出 `position_p_coefficient: 16`）；Amax（`0x55`）出廠 50 → 學姊簡報中跑過 LeRobot 的馬達為 254，對應 `configure_motors(maximum_acceleration=254)`

---

## 六、待驗證（新增）

本節為原始定義。**現況與安排一律以 `docs/verification-list.md` 為準**，該表為唯一的狀態來源。

| 編號 | 項目 | 需真機 |
|---|---|---|
| V-24 | 不同型號馬達（C001 / C044 / C046 / follower 版本）出廠暫存器的差異，特別是 `0x0E`/`0x0F`、`0x10`、`0x12` | 是（snapshot） |
| V-25 | `0x03` 型號碼是否因型號不同而不同 | 是（snapshot） |
| V-26 | 套件附的變壓器實際電壓；`schedule.md` 風險表的寫法是否要改 | 是（看標示） |
| V-27 | 執行 setup/calibrate 的 LeRobot 版本 | 確認即可 |
| V-28 | 若 V-06 發現轉接板有 echo，rustypot 是否正確處理 | 是 |
| V-32 | follower 的 gripper MIN 剛好等於 2047（中點），是否代表 calibrate 沒錄到閉合端 | 是 |

**本週回答掉的既有項目**：V-06（無 echo）、V-15（名稱已知）、V-19（版本不同）、V-25、V-26、V-27。V-24 部分回答（F-44），V-28 因 V-06 的答案而不適用。

---

## 七、計畫變更

**C-31　snapshot 的時機：從「組好後第一件事」改為「setup-motors 完成後、機構組裝與 calibrate 之前」（11/17）**
依據：LeRobot 官方 SO-101 流程是**先**一次一顆設定馬達 ID、**再**組裝。`setup_motor()` 只寫 ID（`0x05`）與鮑率（`0x06`），兩者出廠值已知。
真正改寫其他暫存器的是 calibrate 與 connect：本機原始碼 `motors_bus.py:572` 的 `configure_motors(return_delay_time=0, maximum_acceleration=254, acceleration=254)`，docstring 自述出廠為 250（500 µs）；`configure()` 每次 connect 都會執行，寫 Operating_Mode 與 PID。
新時機的好處：六顆已串好、ID 為 1–6，且不打斷學長的組裝節奏。
`week10.md` 第八節與 `schedule.md` 的「組好後第一件事」依 C-23 不改寫，本條為其更正。
→ 11/20 的 F-42 驗證了這個判斷：設完 ID 之後的 snapshot 等同出廠狀態。

**C-32　snapshot 工具重寫，並規定工具寫好當天進版控（11/17）**
狀況：10/11 寫的 `snapshot.py`、`fake_bus.py` 從未 commit，組裝前才發現已遺失（`week09a.md` 寫「尚未放入 repo」後無人追蹤）。
教訓：與 C-30（`main` 被遺忘）同一類——**東西存在但不在版控可見的地方，等於不存在**。
規則：任何工具或腳本，寫好當天 commit；就算還沒測完，也先進 repo。
重寫時的設計決策：
- 不用任何 SDK，自己組 PING / READ（避開 F-19、F-20；事後證明也避開了 F-30 的後端更換）
- 唯讀兩道防線：組包只允許 `0x01`/`0x02`、送出前再檢查；`fake_bus` 收到其他指令丟例外
- 偵測並記錄 echo（回答 V-06）
- `--ids 1`（單顆出廠馬達）、`--scan`（未知 ID）
- 多讀 `0x47–0x56`（原廠表未列，V-15），讀不到時自動分段

驗證：自我測試 41 項；**反向驗證**——放行 WRITE → 測試失敗；不剝 echo → 測試失敗，而且失敗方式正好就是 F-20（READ 的 echo 被解析成 error byte = `0x02` 的回應）。

**C-33　學習筆記與實驗紀錄分開（11/17）**
- `docs/learning/python-NN-*.md`：離開專題也用得到的知識（觀念、實驗、錯誤速查、Python ↔ C 對照）
- `docs/lab-log/`：專題的決策與發現

每一步一份，依編號累加。附帶規定：Python 與 C 一律 4 格縮排。

---

## 八、檢查點

| 檢查點 | 結果 |
|---|---|
| 出發前準備：清單、snapshot 工具、`lerobot` 環境 | **達成** |
| Python 第 1 步 | **達成** |
| **組裝前出廠快照（只有一次機會）** | **達成** — 12 顆全數取得，且經 F-42 驗證等同出廠狀態 |
| Follower 組裝 + calibrate | **達成**（gripper 範圍待確認，V-32） |
| Leader 組裝 + calibrate | 未執行，順延 |
| Python 第 2–4 步 | 未開始 |
| C 驅動收尾三項 | 本週未做 → 已於 11/23 完成（commit `1b91217`、`7fd24ab`） |
| `interface-spec.md` 交給 B，請他確認 V-21、V-22 | 未確認是否已做 |
| `open-questions.md` 第 8 題補 Python 版本與 `magic` 寫法 | 未做 |

---

## 九、下一步

**在家**
- Python 第 2–4 步
- `docs/factory-registers.md`：把 F-44 的型號族表正式整理成檔（網路上找不到的一手資料）
- 比對 12 顆 JSON 的完整差異，補完 V-24
- `open-questions.md` 第 8 題補 Python 版本與 `magic` 寫法
- 確認 B 是否已收到 `interface-spec.md` 並回報 V-21、V-22

**下次場次（leader 組裝）**
- Leader 組裝 + calibrate。**安全前提**：leader 尚未 calibrate，角度限制仍是出廠的 0/4095，而 `0x2A` 上電為 0（W12 的 F-49）——第一次開扭矩前先把 `0x2A` 寫成當前的 `0x38`
- Follower gripper 範圍確認（V-32），必要時重做 calibrate
- `lerobot-teleoperate` 連動測試
- FD 上下限（若學長要求，calibrate 之後）
- 其餘見 `verification-list.md` 的「下次場次」

**W13**
- 延遲量測。方向已因 F-36、F-37 改變，見 `schedule.md`

---

## 附：meeting 簡報素材

1. **讀原始碼抓到 snapshot 時機的錯誤**（C-31）：在出廠值被改寫**之前**修正，不是事後才發現。主動預防不可逆風險的例子，而且 F-42 事後驗證了判斷正確
2. **後端被換掉，自己的工具不受影響**（F-30 + C-32）：`snapshot.py` 不依賴 SDK，LeRobot 換成 rustypot 一行都不用改。「依賴越少越穩」第二次被驗證（第一次是 C-07 介面凍結）
3. **全新馬達的出廠值和規格書不一樣**（F-36）：讀規格書推算的結論，被實測推翻了一半
4. **文獻的兩個延遲瓶頸都不在我們的硬體上**（F-36、F-37）：這會改變 W13 的量測方向，也牽涉 C-28 的題目範圍，**適合主動帶到教授面前**
5. **出廠暫存器的型號族對照表**（F-44）：網路上找不到的一手資料
6. **`0x07` 可能在 STS3215 上不存在**（F-45）：規格書、FD、LeRobot 三方說法不一，用一個可逆的實驗就能判定。「探討」的好例子——發現文件矛盾 → 設計實驗驗證
7. **安全層論述變得更精準**（F-31）：從「LeRobot 沒檢查」改為「LeRobot 讀到警報但交給上層」，定位清楚
8. **反向驗證重現 F-20**（C-32）：證明測試有鑑別力
9. **10040 實驗與 10.8 ms 週期**（F-33、F-35）：兩個設計決策各有一筆實測依據
10. **工具遺失的教訓**（C-32）：與 C-30 同一類問題，兩次都是「東西存在但不在版控」
11. 照片與圖表：12 顆貼好標籤的馬達並排、CH343P 轉接板、組裝前 snapshot 摘要表、follower calibrate 範圍表
