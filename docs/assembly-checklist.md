# SO-101 組裝檢查清單（W11 組裝場次用）

**撰寫**：2026/11/17　**更新**：2026/11/18（組裝訂於 11/19 晚上；補上現場指令）
**用途**：W11 由學長帶領組裝專題生專用手臂（C-24）。本清單整理官方流程、標出 snapshot 的插入點，並列出現場要記錄的項目，之後當作 `lab-log/week11.md` 的骨架。
**依據**：[LeRobot SO-101 官方文件](https://huggingface.co/docs/lerobot/en/so101)、[Seeed Studio SO-ARM101 Ch.6](https://wiki.seeedstudio.com/robotics/soarm_physical_ai_course_chapter_6/)、LeRobot main 分支原始碼（2026/11/17 閱讀）。本機版本待以 V-19 核對。

---

## 全貌

```
① 認馬達、貼標籤 → ② setup-motors（一次一顆）→ ③ 機構組裝 → ④ calibrate
                                          ↑
                         ★ snapshot：②完成、③開始之前
```

**為什麼是這個時間點**：`setup-motors` 只寫 ID（0x05）與鮑率（0x06），兩者出廠值已知（ID 1、1M）。真正改寫其他暫存器的是 ④：calibrate 寫 homing offset 與角度限制；之後每次 connect 都會執行 `configure()`，寫入 Operating_Mode、PID，並依 `protocol-notes.md` 把 0x07 改為 0、清掉 Phase 0x12。Phase 的出廠值原廠表未列（F-17），**過了 ④ 就再也讀不到**。

在 ② 之後、③ 之前做，六顆已串好、ID 為 1–6，又不打斷學長的組裝節奏。

> 修正：`week10.md` 與 `schedule.md` 原寫「組好後第一件事」。對照官方流程後改為本時間點，於 W11 紀錄以 C 編號登記。

---

## 0. 出發前（在家）

- [x] `snapshot.py`、`fake_bus.py` 已進 repo（原檔遺失，11/17 重寫）
- [x] `python tools/fake_bus.py` 自我測試通過
- [ ] `python tools/snapshot.py --port loop:// --label looptest --ids 1 --rtt 3 --out %TEMP%`：pyserial 真實路徑，應顯示「echo：有」、ID 1 無回應、結束碼 2
- [x] 筆電的 `lerobot` 環境裝好馬達套件（`pip check`、`lerobot-setup-motors --help` 通過）
- [ ] 傳訊息給學長，約好在「設完 ID、開始組裝前」停約 5 分鐘，並問他電腦上的 LeRobot 版本
- [ ] 帶麥克筆、標籤貼紙、手機（拍照）

---

## 1. 認馬達、貼標籤

12 顆外觀幾乎相同，**拆包裝後立刻在外殼寫 F1–F6 / L1–L6**。

**Follower**：6 顆全為 1/345，型號依版本可能是 C001（7.4V）、C018 或 C047（12V）。

**Leader**：三種減速比，不可混用

| 關節 | ID | 型號 | 減速比 |
|---|---:|---|---|
| Shoulder Pan | 1 | C044 | 1:191 |
| Shoulder Lift | 2 | C001 | 1:345 |
| Elbow Flex | 3 | C044 | 1:191 |
| Wrist Flex | 4 | C046 | 1:147 |
| Wrist Roll | 5 | C046 | 1:147 |
| Gripper | 6 | C046 | 1:147 |

- [ ] 實際型號與上表一致
- [ ] 12 顆全部貼好標籤

---

## 2. 電源 ⚠️

- 「7.4V / 12V」是**馬達額定電壓**，不是變壓器電壓。7.4V 的 STS3215 通常配 **5V** 變壓器，12V 馬達配 12V
- 現場變壓器標的很可能是 **5V 與 12V**。標籤寫「Leader 用」「Follower 用」，比寫電壓不易接錯
- 12V 接到 7.4V 馬達可能燒毀
- USB 不供電，電源與 USB 都要接
- 插拔任何線之前先斷電

- [ ] 兩顆變壓器已標示用途
- [ ] 上電前再對一次馬達型號

---

## 3. setup-motors（一次只接一顆）

```bash
lerobot-find-port                      # 找 COM port，依提示拔 USB 後按 Enter
lerobot-setup-motors --robot.type=so101_follower --robot.port=COMx
lerobot-setup-motors --teleop.type=so101_leader  --teleop.port=COMy
```

順序從夾爪往基座：gripper（ID 6）→ wrist_roll（5）→ … → shoulder_pan（1）。

- 每次**只接一顆**，不可與其他馬達串接。出廠 ID 都是 1，同時接多顆會全部被改寫
- 換下一顆：線從控制板拔下（另一端留在上一顆馬達上），拿新線接控制板與下一顆
- 每次按 Enter 前檢查電源線，移動控制板時容易鬆脫
- Waveshare 控制板：兩個跳線帽設在 **B**（USB）

完成後：用 3-pin 線依序串接，ID 1（shoulder pan）接控制板。

**協定視角**：這一步就是對 ID 1 送 WRITE 0x05（ID）與 WRITE 0x06（鮑率），寫進 EEPROM（Lock 0x37 = 0，見 F-15）。

---

## ★ snapshot

**現場指令**（Miniforge Prompt，`conda activate lerobot`，`cd C:\GithubRepo\sts3215-driver`）

```bash
# 1. 找 COM port（插上轉接板前後各跑一次，多出來的那個就是）
python -m serial.tools.list_ports -v

# 2. Follower（COMx 換成實際編號）
python tools/snapshot.py --port COMx --label follower-preassembly --note "setup-motors 後、組裝前；桌上串接；電源 __V"

# 3. Leader
python tools/snapshot.py --port COMy --label leader-preassembly --note "setup-motors 後、組裝前；桌上串接；電源 __V"
```

- 檔案存在 `snapshots/`，終端機會印出摘要表：每顆的型號、韌體、0x07、0x12、電壓門檻、RTT
- 結束碼 2、出現「無回應的 ID」→ 檢查電源與串接，**不要**先跑任何 LeRobot 指令
- 不確定 ID 時加 `--scan`

**檢查**
- [ ] Follower 6 顆都有回應
- [ ] Leader 6 顆都有回應
- [ ] 摘要表的 echo 欄位記下來（V-06）
- [ ] 檔名有 leader / follower，`--note` 填了電源電壓
- [ ] 存檔後才把 USB 還給學長

**順帶取得的資料**
- 每顆馬達的 PING 與三段 READ 原始收送封包（JSON 的 `packets`），就是真機的參考封包。WRITE / SYNC 因唯讀原則不在其中
- 型號差異（V-24、V-25）：12 顆中有 3–4 種型號（C001/C044/C046 + follower 版本），可比較 0x03 型號碼是否都是 777、0x0E/0x0F 電壓門檻、0x10 最大扭矩、0x12 Phase。網路上沒有現成數據，可放進報告

---

## 4. 機構組裝

- 先清掉 3D 列印件的支撐材；每顆馬達先裝好一條 3-pin 線
- 由基座往末端：裝舵盤 → 馬達入支架鎖 M2×6 → 用 M3×6 鎖下一節
- 舵盤上下都裝，**只有上片鎖一顆 M3×6**，下片不鎖
- Leader 20 步、Follower 17 步，差別在第 12 步後的末端（夾爪 / 握把）
- 線插到底、不急彎、不落在關節活動範圍內（W14 長時間穩定性測試時，線被夾導致的斷訊很難和程式問題區分）

---

## 5. calibrate（snapshot 必須已完成）

```bash
lerobot-calibrate --robot.type=so101_follower --robot.port=COMx --robot.id=<名稱>
lerobot-calibrate --teleop.type=so101_leader  --teleop.port=COMy --teleop.id=<名稱>
```

1. 所有關節擺到活動範圍中間 → Enter 記錄中點
2. 每個關節轉過完整範圍 → 結束存檔

- [ ] 記下兩隻手臂的 `id` 名稱與校正檔位置

---

## 現場記錄（W11 紀錄與 meeting 素材）

- [ ] 馬達實際印的型號（拍照）
- [ ] 兩顆變壓器的實際標示電壓
- [ ] 控制板型號、跳線設定、兩隻手臂的 COM port 編號
- [ ] `setup-motors` 的輸出（拍螢幕）
- [ ] 是否遇到 F-23 類 Windows 問題（V-20）
- [ ] 12 顆貼好標籤的馬達並排照（簡報用）
- [ ] 組裝各階段照片
- [ ] 實際花費時間（之後估真機工作量用）

## 待驗證候選（寫入 W11 紀錄時正式編號）

- 不同型號馬達（C001/C044/C046/C018）的出廠暫存器差異
- 0x03 型號碼是否因型號不同而不同
- 套件附的變壓器電壓與 `schedule.md` 風險表「7.4V / 12V」的寫法是否需修正
