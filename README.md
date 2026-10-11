# sts3215-driver

C/C++ 低階串列通訊驅動，用於 VR 遠端遙操作機械手臂的伺服馬達控制層。繞過官方 LeRobot 的 Python 封裝，直接依照 Feetech STS3215 通訊協定手刻封包、收發與安全限位邏輯。

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

## Architecture (planned)

對應 LeRobot / Feetech SDK 的分層，移植至 STM32 時只需替換最底層：

```
Application   安全層、限位檢查、遙操作迴圈
Device        暫存器表、單位換算、sign-magnitude 編解碼
Protocol      封包組裝/解析、checksum、逾時與重傳
HAL           序列埠抽象（PC: termios/Win32 → STM32: UART + DMA → sim: 虛擬馬達）
```

`HAL` 以一組函式指標 `hal_ops_t`（`write` / `read` / `flush_rx` / `millis` + `void *ctx`）定義，PC、STM32 與模擬器各自實作同一組介面，上層三層完全不感知底下接的是什麼。

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
make            # 建置驅動層與範例
make sim        # 建置虛擬馬達模擬器與 demo
```

### 開發規範

Protocol 與 Device 兩層之後要搬上 MCU，因此全程採 MCU 友善寫法：

- 純 C11
- `-Wall -Wextra -Werror -Wconversion`，不容忍警告
- 不使用 `malloc`，所有 buffer 靜態配置或由呼叫者提供
- 不依賴作業系統概念（執行緒、檔案系統、動態載入）
- header 一律加上 `extern "C"` 包裝

## Project status

🚧 In progress — 目前階段：協定研讀完成，進入封包驗證與 C 實作

| 階段 | 週次 | 狀態 |
|---|---|---|
| 協定研讀 | W5 | ✅ 完成 |
| 手臂跑起來 + 介面凍結 | W6 | 進行中 |
| 單軸讀寫 | W7 | 未開始 |
| 單軸完成 + 六軸擴展 | W8–10 | 未開始 |
| 上層封包 + 安全層 + 延遲量測 | W11–13 | 未開始 |
| STM32 移植（條件性） | W14–16 | 未開始 |

**硬承諾**：10/25（W7 結束）前完成單軸 READ / WRITE。其餘項目依進度彈性調整。

**W13 判斷點**：六軸穩定運行 10 分鐘不掉封包，且 HAL 層確實分離，才進入 STM32 移植；未通過則轉向鞏固 PC 版（補測試、補文件）。

硬體現況：leader 與 follower 皆已借到並組裝完成，惟全實驗室僅一組且採預約制，因此開發分為「需真機」與「可離線」兩軌，後者以模擬器支撐。STM32 開發板已取得。

## Repo structure

```
datasheet/    原廠規格書與協定手冊（唯讀參考）
docs/         自行整理的技術筆記與規劃
literature/   已經閱讀過的文獻
ppt/          每週報告與紀錄
include/      對外 header（含 HAL 介面定義）
src/          驅動層原始碼
sim/          虛擬 STS3215 模擬器（離線開發與回歸測試用）
examples/     單軸 / 六軸操作範例
tests/        封包驗證、邊界測試
```

### docs/

| 檔案 | 內容 |
|---|---|
| `protocol-notes.md` | 封包格式、checksum 虛擬碼、指令集、Control Table、逐 byte 拆解範例 |
| `lerobot-notes.md` | 對照 LeRobot 與 Feetech 官方 SDK 的實作，記錄與手冊的落差與修正 |
| `sim-spec.md` | 虛擬馬達模擬器的實作規格與驗收條件 |
| `schedule.md` | 16 週進度規劃、檢查點與風險追蹤 |

### datasheet/

| 檔案 | 說明 |
|---|---|
| `FEETECH_ST-3215_7.4V_Datasheet.pdf` | Leader 馬達規格書 |
| `FEETECH_ST-3215-C018_12V_Datasheet.pdf` | Follower 馬達規格書 |
| `Servo_Protocol_Manual_MagneticEncoding.pdf` | 通訊協定手冊（磁編碼版） |
| `Servo_Protocol_Memory_Table_MagneticEncoding.xlsx` | 記憶體表（磁編碼版） |

## Simulator

手臂採預約制，模擬器讓非使用時段也能在家開發與回歸測試，並把預約到的時間保留給只有真機能做的事（延遲量測、時序驗證）。它同時也是架構分層的驗證：若 HAL 介面設計得乾淨，換成模擬器應當一行上層程式碼都不必改。

模擬器可測封包組裝與解析、收包狀態機、逾時與重傳、sign-magnitude 編解碼、Control Table 讀寫語意，並支援故障注入（不回應、強制 error byte、錯誤 checksum、插入雜訊）；真實延遲、半雙工切換時序與馬達韌體實際行為仍須真機驗證。完整對照表見 `docs/sim-spec.md`。

## Notes

- 協定手冊 1.3.9 的 RESET 範例 checksum 印錯（印 `F6`，正確為 `F2`）；其餘範例封包已全數以程式驗算通過
- 與手冊有出入的實作細節共三處（sign-magnitude 編碼、鮑率對照表、回應延遲暫存器 0x07），均以對照 LeRobot 與原廠 SDK 的結果為準，詳見 `docs/protocol-notes.md` 與 `docs/lerobot-notes.md`

## Reference

- [LeRobot](https://github.com/huggingface/lerobot) — 作初期熟悉、交叉驗證與團隊整合保底之用
- [FTServo_Python](https://github.com/ftservo/FTServo_Python) — Feetech 官方 Python SDK

## License

TBD