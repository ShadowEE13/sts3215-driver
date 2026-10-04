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
HAL           序列埠抽象（PC: termios/Win32 → STM32: UART + DMA）
```

## Project status

🚧 In progress — 目前階段：協定研讀完成，待硬體到位

| 階段 | 週次 | 狀態 |
|---|---|---|
| 協定研讀 + 硬體到位 | W5 | 研讀完成，待借用手臂 |
| 封包驗證 + 單軸讀寫 | W6–7 | 未開始 |
| 單軸完成 + 六軸擴展 | W8–10 | 未開始 |
| 上層封包 + 安全層 + 延遲量測 | W11–13 | 未開始 |
| STM32 移植（條件性） | W14–16 | 未開始 |

W13 設有判斷點：六軸穩定運行 10 分鐘不掉封包且 HAL 層確實分離，才進入 STM32 移植。

## Repo structure

```
datasheet/    原廠規格書與協定手冊（唯讀參考）
docs/         自行整理的技術筆記
src/          驅動層原始碼
examples/     單軸 / 六軸操作範例
tests/        封包驗證、邊界測試
```

### docs/

| 檔案 | 內容 |
|---|---|
| `protocol-notes.md` | 封包格式、checksum 虛擬碼、指令集、Control Table、逐 byte 拆解範例 |
| `lerobot-notes.md` | 對照 LeRobot 與 Feetech 官方 SDK 的實作，記錄與手冊的落差與修正 |

### datasheet/

| 檔案 | 說明 |
|---|---|
| `FEETECH_ST-3215_7.4V_Datasheet.pdf` | Leader 馬達規格書 |
| `FEETECH_ST-3215-C018_12V_Datasheet.pdf` | Follower 馬達規格書 |
| `Servo_Protocol_Manual_MagneticEncoding.pdf` | 通訊協定手冊（磁編碼版） |
| `Servo_Protocol_Memory_Table_MagneticEncoding.xlsx` | 記憶體表（磁編碼版） |

## Notes

協定手冊 1.3.9 的 RESET 範例 checksum 印錯（印 `F6`，正確為 `F2`）；其餘範例封包已全數驗算通過。詳見 `docs/lerobot-notes.md`。

## Reference

- [LeRobot](https://github.com/huggingface/lerobot) — 僅作初期熟悉與交叉驗證用
- [FTServo_Python](https://github.com/ftservo/FTServo_Python) — Feetech 官方 Python SDK

## License

TBD