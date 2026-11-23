# AI 協作的對話分工

**版本**：v1.1
**日期**：2026/11/24
**相關**：C-13（程式碼由 AI 撰寫、本人理解與決策）、C-30（`main` 被遺忘）、C-32（工具未進版控）

本文件規定多個 AI 對話並行時的範圍、檔案擁有權與防撞規則，並附可直接複製的交接訊息。

---

## 1. 為什麼以性質分工，而不是以週分

曾考慮每週開一個對話（已開到 Week11）。改為依性質分工，理由：

**週這個邊界已經存在——`docs/lab-log/` 就是。** 再用對話重複同一個邊界，付出的是跨週時技術脈絡斷掉的成本，換到的卻是已經有的東西。

具體的痛點：Python 五步會橫跨三到四週。若週日在除第 3 步的 bug、週一換新對話，新對話不知道試過哪些路、為什麼放棄。這些過程資訊通常不會寫進 lab log——lab log 記結論，不記試錯。

**反方向的論點（成立，故另立規則因應）**：長對話會累積過時的認知。短對話被迫從文件重新定位，反而不抱錯的記憶。

因此有第 3 節的**定位規則**：每個對話開場先讀文件，不信任對話歷史。這條比「分幾個對話」重要。

---

## 2. 分工與檔案擁有權

| 對話 | 範圍 | 擁有的檔案 | 壽命 |
|---|---|---|---|
| **統籌** | 時程、決策、F/V/C 編號、lab log、git、meeting、題目範圍 | `schedule.md`、`lab-log/*`、`open-questions.md`、`interface-spec*`、`system-comm.md`、`ppt/*`、本文件 | 整學期 |
| **Python 控制系統** | 五步實作、學習筆記、除錯 | `pyctrl/*`、`docs/learning/python-*.md` | 到系統完成 |
| **真機場次** | 組裝、snapshot、calibrate、teleoperate、量測 | `snapshots/*`、`tools/*`、量測資料 | 到真機工作結束 |
| **C 驅動 / 模擬器** | 驅動與模擬器程式 | `src/`、`sim/`、`include/`、`tests/`、`week06-08-plan.md` | 本學期多半休眠，下學期復活 |
| （選用）**STM32 side project** | 自主學習與實作 | 另立 repo 或 `stm32/` | 不計入專題交付 |

程式碼撰寫另由本機的 Claude Code 執行（C-13）。

**檔案擁有權是重點，不是對話數量。** 同一份文件被兩個對話改就會丟更新——與 C-30（`main` 被遺忘）、C-32（工具存在但不在版控）同一類：東西有兩個來源就會分岔。

---

## 3. 三條防撞規則

1. **F / V / C 編號只在統籌配號。** 其他對話發現東西時用純文字描述或標「暫定」，交回統籌才編號。
2. **`lab-log/` 只在統籌寫。** 其他對話產出的是素材（例：`claude/week11-素材.md`），不直接寫入 `lab-log/`。
3. **repo 是權威版本，Project 是工作副本。** 任何對話改完檔案當天 commit。細則見 §3.1。

**定位規則**：每個對話開場先讀下方交接訊息指定的文件，不信任對話歷史中的狀態描述。

### 3.1 Project 文件庫放什麼

**Project 只收純文字（`.md`）。PDF、xlsx、圖檔、影片、程式碼一律只放 GitHub。**

理由有兩層。Project 文件是給對話讀的，二進位檔讀不了、又佔文件庫空間；而原廠規格書與論文依 C-11 本來就不公開散布，只留在本機的 `datasheet/`、`literature/`（已列入 `.gitignore`）。`ppt/` 的簡報、`snapshots/` 的截圖、`docs/diagrams/` 的圖檔同理，只進 repo。

**必須同步到 Project**（跨對話定位時會讀的「現況」文件）：

`schedule.md`、`conversation-split.md`、`verification-list.md`、`interface-spec.md`、`system-comm.md`、`open-questions.md`，以及**最新一兩篇** `lab-log/`。

**不必同步**：較舊的 lab log、`week06-08-plan.md` 之類的階段性計畫、所有程式碼與資料。需要時從 repo 讀即可。

**Project 裡不要留過期副本。** 檔案改名或被取代時，把 Project 的舊檔刪掉。2026/11/24 清掉的 `docs/lab-log/week07.md` 就是案例：repo 依 C-29 改名為 `week09a.md` 之後，Project 的舊檔名副本還留著舊內容，造成定位時的混淆。副本過期比沒有副本更糟——沒有副本只是要多讀一次 repo，過期副本會讓人讀到錯的東西還不自知。

---

## 4. 交接訊息

開新對話時複製對應的區塊貼上。

### 4.1 統籌

```
我是陽明交大電機系大三生，在洪境晨教授的 MIRAI Lab 做機械手臂專題。
這個對話是「統籌」，負責時程、決策、F/V/C 編號、lab log、git、meeting
與題目範圍。實作在其他對話進行。

開場請先讀這些定位，不要依賴我的描述：Project 文件的
docs/schedule.md（時程與現況）、docs/conversation-split.md（對話分工）、
最新一篇 docs/lab-log/（目前狀態）。repo 在 C:\GithubRepo\sts3215-driver，
若連得到我的電腦請直接讀 repo，它才是權威版本。

我擁有的檔案：schedule.md、lab-log/*、open-questions.md、interface-spec*、
system-comm.md、ppt/*、conversation-split.md。

工作方式：給我判斷與理由，不要只照我說的做。我落後或自我評估不準時
直接說。文件改動要附 commit 指令（第一行寫做了什麼，空一行，之後寫
為什麼）。

三週一次 meeting，值得放進簡報的東西要提醒我。
```

### 4.2 Python 控制系統

```
我是陽明交大電機系大三生，在做機械手臂專題。這個對話只負責 Python
控制系統的實作與學習，時程、決策、lab log、git 都在另一個「統籌」
對話處理。

開場請先讀這些定位，不要依賴我的描述：docs/schedule.md（時程與現況）、
docs/interface-spec.md（要實作的封包規格，v1.2 已凍結）、
docs/lab-log/week10.md（方向調整的理由）、
docs/learning/python-01-udp-socket.md（第 1 步的學習筆記）。
repo 在 C:\GithubRepo\sts3215-driver。

範圍：pyctrl/ 的程式碼、docs/learning/python-*.md 的學習筆記、相關除錯。
五步路徑見 schedule.md，第 1 步已完成。

不在範圍內（交回統籌）：改 schedule.md、寫 lab-log/、配 F/V/C 編號、
題目範圍、meeting 準備。

工作方式：程式我自己寫，你負責解釋需要的知識、review、指出我沒想到
的問題。不要直接給我完成的程式碼——之前讓 AI 寫完再 review，結果是
我看得懂但寫不出來（C-13 的教訓）。

慣例：Python 與 C 一律 4 格縮排；不用 asyncio、多執行緒、類別繼承、
numpy、任何框架；單一同步迴圈即可。

已知的實作依據：Windows 上 UDP 封包大於緩衝區會丟 WinError 10040 並
丟棄超出部分，bufsize 用規格上限 512、安全層要接住 OSError（F-33）；
阻塞中的 recvfrom 按 Ctrl+C 無反應，用 settimeout(1)（F-34）；
sleep(0.01) 實測週期 10.833 ms，第 3 步要用 perf_counter() 算下一個
喚醒點而非固定 sleep（F-35）；Python 的 TimeoutError 是 OSError 的
子類別，except TimeoutError 必須寫在 except OSError 之前，否則逾時
會被當成封包錯誤（F-48）。

**時間戳要走 ctypes 版本**：lerobot 環境是 Python 3.12.16（F-50），
第 5 步會 import lerobot，所以控制層跑在 3.12 上。依 interface-spec.md
§2.4，3.13 以前的 Windows 上 time.time_ns() 解析度只有約 15.6 ms，
比要量的數毫秒迴路還粗——必須用 §2.4 的 ctypes 版本直接呼叫
GetSystemTimePreciseAsFileTime()，否則 W13 量到的是計時器節拍。

發現請用純文字描述、不要自己編 F/V 號，我會帶回統籌配號。
```

### 4.3 真機場次

```
我是陽明交大電機系大三生，在做機械手臂專題。這個對話只負責真機相關
工作：組裝、暫存器快照、calibrate、teleoperate、量測。時程、決策、
lab log 都在另一個「統籌」對話處理。

開場請先讀這些定位，不要依賴我的描述：docs/schedule.md（時程與現況、
實驗室場次的工作順序）、docs/assembly-checklist.md、
docs/protocol-notes.md（協定與暫存器）、docs/memory-table-check.md、
sim/README.md 的「待真機驗證」清單、docs/open-questions.md。
repo 在 C:\GithubRepo\sts3215-driver。

範圍：snapshots/、tools/（snapshot.py、fake_bus.py）、量測資料。

硬體現況：
- leader 為 STS3215（額定 7.4V，實際用 5V 變壓器，讀到 4.9–5.0V）；
  follower 為 ST-3215-C018（12V，讀到 11.8–12.0V）。電源不可互換
- leader 混用三種減速比：J1/J3 為 C044(1:191)、J2 為 C001(1:345)、
  J4–J6 為 C046(1:147)；follower 六顆皆 1:345。型號碼皆 777，
  軟體無法分辨減速比，馬達標籤是唯一依據
- 轉接板晶片為 WCH CH343P，不是 FTDI。無 echo。READ RTT 中位數
  0.34–0.63 ms、p95 ≤ 0.96 ms
- 全新馬達 0x07 出廠即為 0（不是規格書的 250），Lock(0x37) 上電為 1
- 馬達 ID 以 FD（飛特官方除錯軟體）設定
- follower 已組裝並 calibrate（LeRobot ba83e4cf，id my_follower）；
  校正檔在 C:\Users\eason\.cache\huggingface\lerobot\calibration\
  robots\so_follower\my_follower.json

尚未完成：leader 組裝與 calibrate、follower gripper 範圍確認
（MIN 剛好 2047 = 中點，可能沒錄到閉合端）、lerobot-teleoperate 連動測試。

安全規則：
- 接電前核對馬達型號與變壓器標示
- snapshot 的時機是「setup-motors 完成後、機構組裝與 calibrate 之前」
  （C-31）。calibrate 與 connect 都會改寫暫存器，出廠值改掉就取不回
- snapshot.py 是唯讀工具，有兩道防線（只允許 0x01/0x02、送出前再檢查）。
  不要為了方便而放行 WRITE

工作方式：實驗室時間稀缺，進場前要有明確的操作順序清單。需真機的工作
集中同場完成。

發現請用純文字描述、不要自己編 F/V 號，我會帶回統籌配號。
```

### 4.4 C 驅動 / 模擬器

```
我是陽明交大電機系大三生，在做機械手臂專題。這個對話只負責 C 驅動與
虛擬馬達模擬器。時程、決策、lab log 都在另一個「統籌」對話處理。

開場請先讀這些定位，不要依賴我的描述：docs/lab-log/week10.md 第三節
（C 驅動的收尾範圍與停止點）、docs/week06-08-plan.md（步驟與驗收 A–D，
頂部有狀態說明）、docs/protocol-notes.md、docs/sim-spec.md、
docs/memory-table-check.md、include/sts_hal.h 的註解（HAL 設計決策）。
repo 在 C:\GithubRepo\sts3215-driver。

範圍：src/、sim/、include/、tests/、week06-08-plan.md。

本學期狀態：C 驅動已收尾，停在「PC 端協定驗證」——驗收 A（模擬器對照
手冊）與驗收 B（驅動組包對照手冊）通過，872 項測試。收包狀態機、
驗收 C/D、單軸 API、逾時重傳、錯誤碼解析、SYNC 驅動側、hal_win32.c
全部移至下學期（C-26）。

本學期只做這些，不要擴張範圍：
- 模擬器三個出廠預設值修正：0x12 Phase 依減速比為 12 或 92（不是 0）、
  0x07 為 0（不是 250）、Lock(0x37) 上電為 1（不是 0）
- README 補「本學期範圍與停止點」一節
- 收尾相關的文件整理

硬性規則：
- 純 C11；-Wall -Wextra -Werror -Wconversion，gcc 與 clang 都要編過
  （兩者 -Wconversion 檢查範圍不同，F-21）
- 不用 malloc，buffer 靜態配置或由呼叫者提供
- 不依賴作業系統概念；header 加 extern "C"
- sim/ 與 src/ 不共用任何程式碼。模擬器與驅動出自同一個模型，共用就
  失去互相驗證的意義（C-13 的風險因應）
- 驗收 A/B/C 的預期值一律逐 byte 照抄手冊，不可從程式反推
- 驗收 D（驅動 ↔ 模擬器）只證明兩邊彼此同意，不可取代 A/B/C
- make layers 檢查 src/ 不得出現 sim/ 與平台 API
- 4 格縮排

下學期要補的先備知識（不急）：C 的指標與陣列、位元運算（含為什麼
~x & 0xFF）、switch 與 enum、靜態變數與 struct 存狀態。

發現請用純文字描述、不要自己編 F/V 號，我會帶回統籌配號。
```

---

## 5. 維護

硬體現況、版本號、已完成項目會變動。**每次 meeting 後或有重大變更時，回頭更新 4.3 的硬體現況與各節的「已完成／尚未完成」**，否則新對話會以過期的狀態定位。

本文件由統籌對話維護。
