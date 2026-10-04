# STS3215 通訊協定筆記

資料來源：
- `datasheet/Servo_Protocol_Manual_MagneticEncoding.pdf`（指令集、封包格式）
- `datasheet/Servo_Protocol_Memory_Table_MagneticEncoding.xlsx`（Control Table）
- `datasheet/FEETECH_ST-3215_7.4V_Datasheet.pdf`（leader 電氣規格）
- `datasheet/FEETECH_ST-3215-C018_12V_Datasheet.pdf`（follower 電氣規格）

---

## 1. 通訊基本參數

| 項目 | 規格 |
|---|---|
| 實體層 | 半雙工非同步串列（TTL / RS485 相容） |
| 幀格式 | 1 start bit + 8 data bits + 1 stop bit，無 parity，共 10 bits |
| ID 範圍 | 0–253（0x00–0xFD），254（0xFE）為廣播 ID |
| 鮑率 | 出廠預設 1 Mbps，可設定 38400bps–1Mbps（地址 0x06） |
| 多字節數值順序 | **低位元組在前，高位元組在後**（little-endian） |
| 負數表示法 | **sign-magnitude**（最高位為方向位），不是二補數；詳見 `lerobot-notes.md` 4.1 |
| 編碼器解析度 | 12-bit，0–4095 對應 0–360°，中位 2048，解析度 0.088°/step |

> ⚠️ 廣播 ID 注意：用 0xFE 發送指令時，除了 PING 以外都不會收到回應包。多顆馬達掛同一條 bus 時不能用廣播 PING。

---

## 2. 封包格式

### 2.1 指令包（Controller → Servo）

| 欄位 | Header | Header | ID | Length | Instruction | Param1...ParamN | Checksum |
|---|---|---|---|---|---|---|---|
| Byte | 0xFF | 0xFF | ID | Length | Instruction | — | Checksum |

- **Header**：連續兩個 `0xFF`
- **ID**：目標舵機 ID
- **Length**：= 參數個數 N + 2
- **Instruction**：功能碼（見第 3 節）
- **Checksum**：見 2.3

### 2.2 應答包（Servo → Controller）

| 欄位 | Header | Header | ID | Length | Error(狀態) | Param1...ParamN | Checksum |
|---|---|---|---|---|---|---|---|
| Byte | 0xFF | 0xFF | ID | Length | Error | — | Checksum |

- `Error` 為 0 表示無錯誤；bit 定義見第 5 節「舵機狀態」
- 若原指令是 READ DATA，Param1...ParamN 即為讀出的資料

### 2.3 Checksum 虛擬碼

```
checksum = (ID + Length + Instruction + Param1 + ... + ParamN)
checksum = checksum & 0xFF      # 若超過 255，取最低一個 byte
checksum = (~checksum) & 0xFF   # 取反（bitwise NOT）
```

寫 C 時注意：`~` 在 C 裡對 `int` 操作要記得 `& 0xFF` 截斷，否則高位會汙染結果。

---

## 3. 指令集

| 指令 | 功能 | Code | 最小參數長度 | 備註 |
|---|---|---|---|---|
| PING | 查詢工作狀態 | 0x01 | 0 | 支援廣播，仍會回應 |
| READ DATA | 讀取 Control Table | 0x02 | 2 | Param: [首地址, 長度] |
| WRITE DATA | 寫入 Control Table | 0x03 | ≥1 | Param: [首地址, data...] |
| REG WRITE | 異步寫（先緩衝） | 0x04 | ≥2 | 要搭配 ACTION 才真正執行 |
| ACTION | 觸發 REG WRITE | 0x05 | 0 | 搭配廣播 ID，多軸同動用 |
| SYNC WRITE | 同步寫多顆舵機 | 0x83 | ≥2 | 同一指令一次改多顆 |
| SYNC READ | 同步讀多顆舵機 | 0x82 | ≥3 | 回應包依 ID 順序逐包返回 |
| RECOVERY | 恢復出廠值 | 0x06 | 0 | |
| RESET | 重置狀態（含圈數） | 0x0A | 0 | |

> ⚠️ 手冊 1.3.9 RESET 範例 `FF FF 01 02 0A F6` 的 checksum 印錯，正確是 `F2`。其餘範例已用程式全部驗算過。

**第五週優先實作順序**：PING → READ DATA → WRITE DATA。SYNC WRITE 留到第 8–10 週擴展六軸時再做（現在逐軸 WRITE 就夠跑通基本功能）。

---

## 4. 指令範例（逐 byte 拆解，可直接拿來對照驗證）

### 4.1 PING — 查詢 ID=1 狀態

```
發送: FF FF 01 02 01 FB
      │  │  │  │  │  └─ checksum = ~(01+02+01) = ~04 = 0xFB
      │  │  │  │  └──── instruction = PING (0x01)
      │  │  │  └─────── length = 2
      │  │  └────────── id = 1
      └──┴───────────── header

回應: FF FF 01 02 00 FC   (error=0x00 → 無錯誤)
```

### 4.2 READ DATA — 讀 ID=1 當前位置（地址 0x38，長度 2）

```
發送: FF FF 01 04 02 38 02 BE
      instruction=0x02, param=[0x38(首地址), 0x02(長度)]
      checksum = ~(01+04+02+38+02) & 0xFF = 0xBE

回應: FF FF 01 04 00 18 05 DD
      error=0x00, data = [0x18, 0x05]
      → little-endian 合成 = 0x0518 = 1304（十進位位置）
```

### 4.3 WRITE DATA — 控制 ID=1 以速度 1000 步/秒 轉到位置 2048

```
發送: FF FF 01 09 03 2A 00 08 00 00 E8 03 D5

拆解 instruction=0x03(WRITE), 首地址=0x2A(目標位置暫存器)
  param: 2A | 00 08 | 00 00 | E8 03
         │     │       │       └─ 速度 0x03E8 = 1000 (little-endian)
         │     │       └───────── 時間 0x0000 = 0
         │     └───────────────── 位置 0x0800 = 2048 (little-endian)
         └─────────────────────── 寫入首地址 = 目標位置 (0x2A)

回應: FF FF 01 02 00 FC  (error=0, 已開始執行)
```

> 這三個例子（+ datasheet 附的 SYNC WRITE/READ 範例）就是 checksum 虛擬碼的驗證素材：先手算一遍，再跟官方範例比對，吻合了再開始寫 C。

---

## 5. 關鍵 Control Table 暫存器（精簡版，完整表在 xlsx 原檔）

### 5.1 EPROM 區（掉電保存，需先關閉鎖才能改且較耗寫入壽命）

| 地址 (hex) | 功能 | 長度 | 讀寫 | 範圍 / 單位 | 備註 |
|---|---|---|---|---|---|
| 0x05 | ID | 1 | 讀寫 | 0–253 | 同 bus 不可重複 |
| 0x03 | 型號碼 | 2 | 只讀 | — | 當 16-bit 讀，sts3215 = 777 |
| 0x06 | 波特率 | 1 | 讀寫 | 0–7 | 1M/500k/250k/128k/115200/76800/57600/38400（LeRobot 的表 5–7 有誤） |
| 0x07 | 回應延遲 | 1 | 讀寫 | 2µs 單位 | 預設 250（500µs）；LeRobot 改 0，但 xlsx 說 STS 無此功能，待驗證 |
| 0x09 | 最小角度限制 | 2 | 讀寫 | 0–4094 步 | 安全層要用：軟體限位下界 |
| 0x0B | 最大角度限制 | 2 | 讀寫 | 1–4095 步 | 安全層要用：軟體限位上界 |
| 0x0D | 最高溫度上限 | 1 | 讀寫 | 0–100 °C | |
| 0x0E / 0x0F | 最高/最低輸入電壓 | 各1 | 讀寫 | 0.1V 單位 | 過壓/欠壓保護門檻，**7.4V/12V 版要分別設定** |
| 0x10 | 最大扭矩 | 2 | 讀寫 | 0–1000 (=0.1%) | |
| 0x12 | 相位（Phase） | 1 | 讀寫 | bitmask | BIT4=1 為多圈回授；LeRobot 會清成 0 強制單圈 0–4095 |
| 0x13 | 卸載條件 | 1 | 讀寫 | bitmask | 各類保護開關，見 5.3 |
| 0x1F | 位置校正（Homing Offset） | 2 | 讀寫 | 步 | sign bit = BIT11 |

### 5.2 SRAM 區（即時控制 / 回授，不掉電保存）

| 地址 (hex) | 功能 | 長度 | 讀寫 | 單位 | 備註 |
|---|---|---|---|---|---|
| 0x28 | 扭矩開關 | 1 | 讀寫 | — | 0=關 1=開 128=當前位置校正為2048 |
| 0x29 | 加速度 | 1 | 讀寫 | 100步/s² | 原廠 SDK 從這裡開始一次寫 7 bytes（acc + 位置 + 時間 + 速度） |
| 0x2A | **目標位置** | 2 | 讀寫 | 步 | WRITE 範例用的欄位；sign bit = BIT15 |
| 0x2C | 運行時間 | 2 | 讀寫 | 0–1000（千分比） | 只有 PWM 開環模式用，**位置模式填 0** |
| 0x2E | 運行速度 | 2 | 讀寫 | 步/s | 50步/s ≈ 0.732 RPM；sign bit = BIT15 |
| 0x30 | 轉矩限制 | 2 | 讀寫 | 0–1000（1000 = 100%） | 上電時由 0x10 複製過來 |
| 0x37 | 鎖標誌 | 1 | 讀寫 | — | 寫 0 才能把值寫入 EPROM 並掉電保存 |
| **0x38** | **當前位置** | 2 | 只讀 | 步 | READ 範例用的欄位 |
| 0x3A | 當前速度 | 2 | 只讀 | 步/s | sign bit = BIT15 |
| 0x3C | 當前負載 | 2 | 只讀 | 0.1% | sign bit = BIT10 |
| 0x3E | 當前電壓 | 1 | 只讀 | 0.1V | |
| 0x3F | 當前溫度 | 1 | 只讀 | °C | |
| **0x41** | **舵機狀態（Error Byte）** | 1 | 只讀 | bitmask | 見 5.3 |
| 0x45 | 當前電流 | 2 | 只讀 | 6.5mA 單位 | |

### 5.3 狀態 / 保護 Bitmask（地址 0x41、0x13、0x14 共用同一組 bit 定義）

| Bit | 意義 | 0 | 1 |
|---|---|---|---|
| BIT0 | 電壓 | 正常 | 過壓/欠壓 |
| BIT1 | 磁編碼 | 正常 | 磁編碼異常 |
| BIT2 | 溫度 | 正常 | 過熱 |
| BIT3 | 電流 | 正常 | 過流 |
| BIT4 | （保留） | — | — |
| BIT5 | 負載 | 正常 | 過載 |
| BIT6/7 | （保留） | — | — |

> 地址 0x41 讀到非 0 就代表馬達出狀況了，解析這個 byte 是「錯誤碼解析」（第 8–10 週待辦）的基礎，安全層的異常偵測也直接讀這個暫存器。

---

## 6. 要用硬體才能確認的問題清單

- [ ] 實測 checksum：拿官方 LeRobot 送一包真實指令，序列埠監聽擷取，跟上面公式手算比對
- [ ] 確認 follower (12V) 的 0x0E/0x0F 過壓/欠壓門檻出廠預設值，是否需要依電源重新設定
- [x] ~~確認 0x2C「運行時間」欄位的實際單位~~ → 已由原廠 SDK 釐清：位置模式填 0
- [ ] 量測單包 WRITE/READ 的實際往返時間，作為第 11–13 週延遲量測的基準
- [ ] 其他對照 LeRobot 後新增的驗證項目，見 `lerobot-notes.md` 第 9 節

## 7. 下一步（對應第 6–7 週）

1. 用 LeRobot 送指令 + 序列埠監聽，擷取真實封包，逐 byte 比對本筆記的 checksum 計算
2. 開始 C/C++ 實作：開序列埠 → 組 WRITE 封包 → 送出 → 解析 READ 回應
3. 硬性檢查點：手動算出的 checksum 要跟官方送出的完全一致，過了才往下走