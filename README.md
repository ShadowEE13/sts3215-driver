# sts3215-driver

C/C++ 低階串列通訊驅動，用於 VR 遠端遙操作機械手臂的伺服馬達控制層。繞過官方 LeRobot 的 Python 封裝，直接依照 Feetech STS3215 通訊協定手刻封包、收發與安全限位邏輯。

> **本學期停在「PC 端協定驗證」**：驅動尚未在真機上執行，見[本學期範圍與停止點](#本學期範圍與停止點)。

## Background

這是 NYCU MIRAI Lab 數位孿生專題的子題目：透過 VR、視覺偵測與機械手臂實現遠端同步操作。本 repo 負責 PC 端到手臂的底層通訊與控制層，目標是建立一份可直接作為嵌入式系統作品集的驅動程式碼，之後視進度移植至 STM32。

## Hardware

- **Arm**：SO-ARM101，6-DOF，leader / follower 各一
- **Servo**：Feetech STS3215 系列，12-bit 磁性編碼器（0–4095 對應 360°），半雙工 TTL 串列匯流排
  - Leader：`STS3215`，7.4V / 19.5 kg·cm，工作電壓 4–7.4V
  - Follower：`ST-3215-C018`，12V / 30 kg·cm，工作電壓 4–14V
  - ⚠️ 兩者工作電壓範圍不同，**電源供應器不可互換**（12V 接上 leader 會觸發過壓保護或燒毀）
- **馬達 ID**：1 shoulder_pan / 2 shoulder_lift / 3 elbow_flex / 4 wrist_flex / 5 wrist_roll / 6 gripper
- **Protocol**：半雙工非同步串列，8N1 無校驗，ID 0–253（254 為廣播），鮑率 38400bps–1Mbps（出廠預設 1Mbps）
- 多位元組數值為 little-endian；位置、速度、負載等有號值採 **sign-magnitude** 編碼，非二補數

## Literature

遙操作的體感延遲由控制迴圈頻率決定，而匯流排讀寫是這個迴圈的瓶頸。兩份來源分別解釋了瓶頸的兩段：

**Bestmann et al. (2019)** — RoboCup 人形機器人，20 顆 Dynamixel 掛同一條半雙工匯流排。提出的週期模型為「傳輸時間 + 馬達數 × 每顆回應延遲」，預測 150 Hz、實測 149 Hz。兩個反直覺的結論：回應延遲不隨鮑率縮短（馬達數多時會成為主導項）；MCU 轉發封包反而比純 USB 轉序列慢。

**FTDI AN232B-04** — USB 轉序列晶片為排程輪詢而非中斷驅動，且需緩衝區滿 62 bytes 或 latency timer 逾時才回傳主機，預設 16 ms。

**套用到本系統**：一個遙操作週期為 SYNC READ + 六個回應包 + SYNC WRITE，共 88 bytes，1 Mbps 下是 0.88 ms。但若回應延遲暫存器 `0x07` 維持出廠預設 500 µs，六顆即佔 3 ms（週期的 77%），頻率掉至 260 Hz；而單次六軸 SYNC READ 僅回 48 bytes，低於 62 bytes 門檻，每次讀取都會撞上 latency timer——預設 16 ms 時頻率上限僅 50–60 Hz。

**缺口**：兩份來源都未測過「MCU 獨立控制、PC 完全不在控制迴圈內」的組態。因此規劃 A/B/C 三組對照（timer 預設 / timer 調最低 / STM32 獨立），W14–16 的移植階段同時是在補這個對照組。

## Architecture (planned)

對應 LeRobot / Feetech SDK 的分層，移植至 STM32 時只需替換最底層：

```
Application   安全層、限位檢查、遙操作迴圈
Device        暫存器表、單位換算、sign-magnitude 編解碼
Protocol      封包組裝/解析、checksum、逾時與重傳
HAL           序列埠抽象（PC: termios/Win32 → STM32: UART + DMA → sim: 虛擬馬達）
```

`HAL` 以一張函式表 `hal_ops_t`（`write` / `read` / `flush_rx` / `millis`，第一個參數皆為 `void *ctx`）定義，宣告成 `static const`；一條 bus 的實例為 `sts_hal_t`（函式表指標 + `ctx`），leader 與 follower 共用同一張表。PC、STM32 與模擬器各自實作同一組介面，上層三層完全不感知底下接的是什麼。

### 上層介面與後端切換

對隊友（VR 端、視覺端）公開一組凍結的上層介面，背後可切換兩種後端：

```
         VR 端 / 視覺端
              ↓  凍結的上層介面（含 CmdType 欄位）
      ┌───────┴───────┐
  LeRobot 後端      本 repo 的 C 驅動
  （團隊保底線）      （本子題目成果）
```

此設計讓團隊整合不受本驅動的開發進度影響，兩者可並行推進。

## Build

```sh
make test        # 編譯並執行全部測試，含驗收 A（sim/demo.c）
make demo        # 只跑模擬器自我驗證，印出收送的每個 byte
make layers      # 分層檢查：src/ 不得引用 sim/ 或平台 API
make SAN=1 test  # 加上 AddressSanitizer + UBSan
make clean && make CC=clang test   # 用 clang 再編一次：-Wconversion 的檢查範圍與 gcc 不同
```

### 開發規範

Protocol 與 Device 兩層之後要搬上 MCU，因此全程採 MCU 友善寫法：

- 純 C11
- `-Wall -Wextra -Werror -Wconversion`，不容忍警告
- 不使用 `malloc`，所有 buffer 靜態配置或由呼叫者提供
- 不依賴作業系統概念（執行緒、檔案系統、動態載入）
- header 一律加上 `extern "C"` 包裝

## 本學期範圍與停止點

**停止點：PC 端協定驗證**（2026/11/15，C-26）。驅動**還沒有在真機上執行過**：本學期的驗證全部在 PC 上完成，對照的是協定手冊與模擬器。

### 做到哪裡

| 項目 | 狀態 | 怎麼驗證 |
|---|---|---|
| HAL 介面 `include/sts_hal.h` | 完成 | 函式表 + `ctx`，設計理由寫在 header 註解；`tests/test_hal.c` |
| 虛擬 STS3215 模擬器 `sim/` | 完成 | Control Table、PING / READ / WRITE / SYNC READ / SYNC WRITE、故障注入、虛擬時鐘 |
| 驗收 A：手冊範例封包 → 模擬器 | ✅ 通過（tag `accept-A`） | 手冊 4.1–4.3 的回應逐 byte 相同（`sim/demo.c`） |
| 驗收 B：驅動組包 vs 手冊 | ✅ 通過（tag `accept-B`） | 同上三包逐 byte 相同；另做反向驗證（故意寫錯 checksum，測試會失敗）與 Python 獨立重寫對照 |
| Protocol 層組包、Device 層 sign-magnitude 與 little-endian | 完成 | `tests/test_protocol.c`、`tests/test_device.c` |
| 測試 | 881 項全過 | gcc / clang 在 `-Werror -Wconversion` 下零警告，ASan + UBSan 無錯誤 |

模擬器的出廠預設值以組裝前的唯讀快照（`snapshots/20261119_*-preassembly.json`，12 顆全新馬達）為準，與規格書或原廠記憶體表不同的地方以實測為準：`0x07` = 0、Phase `0x12` = 12（1:345）或 92（1:191、1:147）、Lock `0x37` 上電為 1、目標位置 `0x2A` 上電為 0（不是當前位置）。

### 為什麼停在這裡

- 本學期改以 Python 建立可對接的系統為主線（C-25），伺服機通訊先交給 LeRobot。上層介面 [`docs/interface-spec.md`](docs/interface-spec.md) 不受影響，隊友那端無感
- 剩下的步驟全都依賴收包狀態機，而它需要先補 C 的基礎（指標與陣列、位元運算、`switch` 與 enum、用 struct 保存狀態）。所以整條路線一起移到下學期，不留做到一半的待辦
- 停在這裡是一個完整的階段：「協定理解正確」已經有兩道對照手冊的外部證據，A 從模擬器那側、B 從驅動這側，兩者都不是拿彼此當參照

### 移到下學期

| 步驟 | 內容 | 依賴 |
|---|---|---|
| 6 | 收包狀態機 + 驗收 C（手冊回應包 → 驅動解析） | C 的先備知識 |
| 7 | 單軸 API `sts_ping` / `sts_read` / `sts_write` + 驗收 D（驅動 ↔ 模擬器） | 6 |
| 8–9 | 逾時與重傳、錯誤碼解析 | 6 |
| 10–11 | 故障注入的驅動側測試、SYNC READ / SYNC WRITE 驅動側 | 6、7 |
| — | `src/hal_win32.c`、真機 bring-up | 以上全部 |

**下學期從這裡接**：[`docs/week06-08-plan.md`](docs/week06-08-plan.md) 步驟 6。演算法（`docs/LeRobot-notes.md` 第 5 節）、測資（驗收 C 的五項）、模擬器與故障注入都已就緒。驗收 D 只證明驅動與模擬器彼此同意，不能取代 A / B / C。

時程與決策紀錄見 [`docs/schedule.md`](docs/schedule.md) 與 [`docs/lab-log/week10.md`](docs/lab-log/week10.md) 第三節。

## Repo structure

```
docs/         自行整理的技術筆記與規劃
ppt/          每週報告與紀錄
include/      對外 header（含 HAL 介面定義）
src/          驅動層原始碼
sim/          虛擬 STS3215 模擬器（離線開發與回歸測試用）
tests/        封包驗證、邊界測試
pyctrl/       Python 控制系統（本學期主線）
tools/        真機工具（唯讀暫存器快照等）
snapshots/    真機暫存器快照與截圖
```

### docs/

| 檔案 | 內容 |
|---|---|
| [`protocol-notes.md`](docs/protocol-notes.md) | 封包格式、checksum 虛擬碼、指令集、Control Table、逐 byte 拆解範例 |
| [`LeRobot-notes.md`](docs/LeRobot-notes.md) | 對照 LeRobot 與 Feetech 官方 SDK 的實作，記錄與手冊的落差與修正 |
| [`sim-spec.md`](docs/sim-spec.md) | 虛擬馬達模擬器的實作規格與驗收條件 |
| [`schedule.md`](docs/schedule.md) | 16 週進度規劃、檢查點與風險追蹤 |
| [`lab-log/`](docs/lab-log/) | 每週實驗記錄：發現（F-nn）、待驗證（V-nn）、計畫變更（C-nn） |

### ppt/

每週進度報告與講稿，非程式碼產出。

## Simulator

手臂採預約制，模擬器讓非使用時段也能在家開發與回歸測試，並把預約到的時間保留給只有真機能做的事（延遲量測、時序驗證）。它同時也是架構分層的驗證：若 HAL 介面設計得乾淨，換成模擬器應當一行上層程式碼都不必改。

模擬器可測封包組裝與解析、收包狀態機、逾時與重傳、sign-magnitude 編解碼、Control Table 讀寫語意，並支援故障注入（不回應、強制 error byte、錯誤 checksum、插入雜訊）；真實延遲、半雙工切換時序與馬達韌體實際行為仍須真機驗證。完整對照表見 [`docs/sim-spec.md`](docs/sim-spec.md)。

## Notes

- 協定手冊 1.3.9 的 RESET 範例 checksum 印錯（印 `F6`，正確為 `F2`）；其餘範例封包已全數以程式驗算通過
- 與手冊有出入的實作細節共三處（sign-magnitude 編碼、鮑率對照表、回應延遲暫存器 0x07），均以對照 LeRobot 與原廠 SDK 的結果為準，詳見 [`docs/protocol-notes.md`](docs/protocol-notes.md) 與 [`docs/LeRobot-notes.md`](docs/LeRobot-notes.md)
- 原廠文件稱 STS 系列的 `0x07` 無回應延遲功能，但 LeRobot 的程式碼會寫入此位址，兩方說法不一致，待真機實測

## Reference

原廠文件與論文受著作權保護，不收錄於本 repo，請依下列來源自行取得。本地可放在 `datasheet/`、`literature/`，兩個資料夾已列入 `.gitignore`。

### 原廠文件

皆可於 [飛特（Feetech）官網](https://www.feetechrc.com/) 下載。

| 文件 | 用途 |
|---|---|
| STS3215 規格書（7.4V / 19.5 kg·cm） | Leader 馬達電氣規格 |
| ST-3215-C018 規格書（12V / 30 kg·cm） | Follower 馬達電氣規格 |
| Servo Protocol Manual（磁編碼版） | 封包格式、指令集 |
| Servo Protocol Memory Table（磁編碼版） | Control Table 暫存器表 |

### 程式碼

- [LeRobot](https://github.com/huggingface/lerobot) — 作初期熟悉、交叉驗證與團隊整合保底之用
- [FTServo_Python](https://github.com/ftservo/FTServo_Python) — Feetech 官方 Python SDK

### 文獻

**串列匯流排通訊與延遲**（直接影響驅動設計，見上方 Literature 一節）

- M. Bestmann, J. Güldenstein, and J. Zhang, "High-Frequency Multi Bus Servo and Sensor Communication Using the Dynamixel Protocol," in *RoboCup 2019: Robot World Cup XXIII*, LNCS vol. 11531, pp. 16–29, Springer, 2019. [[PDF]](https://robocup.informatik.uni-hamburg.de/wp-content/uploads/2019/06/dxl_paper-4.pdf)
- FTDI, "AN232B-04: Data Throughput, Latency & Handshaking," Application Note, v1.1, 2006. [[FTDI Application Notes]](https://ftdichip.com/document/application-notes/)

**遙操作系統**

- P. F. Hokayem and M. W. Spong, "Bilateral teleoperation: An historical survey," *Automatica*, 2006. [[DOI]](https://doi.org/10.1016/j.automatica.2006.06.027)
- T. Z. Zhao, V. Kumar, S. Levine, and C. Finn, "Learning Fine-Grained Bimanual Manipulation with Low-Cost Hardware," in *Robotics: Science and Systems (RSS)*, 2023. [[arXiv]](https://arxiv.org/abs/2304.13705)
- P. Wu, Y. Shentu, Z. Yi, X. Lin, and P. Abbeel, "GELLO: A General, Low-Cost, and Intuitive Teleoperation Framework for Robot Manipulators," arXiv:2309.13037, 2023. [[arXiv]](https://arxiv.org/abs/2309.13037)
- F. P. Audonnet, J. Grizou, A. Hamilton, and G. Aragon-Camarasa, "TELESIM: A Modular and Plug-and-Play Framework for Robotic Arm Teleoperation using a Digital Twin," arXiv:2309.10579, 2023. [[arXiv]](https://arxiv.org/abs/2309.10579)

## License

TBD