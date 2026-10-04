# sts3215-driver

C/C++ 低階串列通訊驅動，用於 VR 遠端遙操作機械手臂的伺服馬達控制層。繞過官方 LeRobot 的 Python 封裝，直接依照 Feetech STS3215 通訊協定手刻封包、收發與安全限位邏輯。

## Background

這是 NYCU MIRAI Lab 數位孿生專題的子題目：透過 VR、視覺偵測與機械手臂實現遠端同步操作。本 repo 負責 PC 端到手臂的底層通訊與控制層，目標是建立一份可直接作為嵌入式系統作品集的驅動程式碼，之後視進度移植至 STM32。

## Hardware

- **Arm**: SO-ARM101（6-DOF follower + leader）
- **Servo**: Feetech STS3215，12-bit 磁性編碼器，半雙工 TTL 串列匯流排
  - Leader: 7.4V 版（ST3215, 19.5kg·cm）
  - Follower: 12V 版（ST-3215-C047, 30kg·cm）— 兩者工作電壓範圍不同，**電源不可互換**
- **Protocol**: 半雙工非同步串列，8N1，ID 0–253，鮑率 38400bps–1Mbps（預設 1Mbps）

## Project status

🚧 In progress — 目前階段：協定研讀與單軸封包驗證

| 階段 | 狀態 |
|---|---|
| 協定研讀 + 硬體到位 | 進行中 |
| 封包驗證 + 單軸讀寫 | 未開始 |
| 六軸同步擴展 | 未開始 |
| 上層封包 + 安全層 | 未開始 |
| STM32 移植（條件性） | 未開始 |

## Repo structure (planned)