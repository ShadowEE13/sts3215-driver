# Python 02：struct 與封包解析

**日期**：2026/11/23（W12）
**對應**：Python 第 2 步（`pyctrl/receiver.py` 加上解析）
**環境**：Windows、Miniforge。本步驟不需要 `lerobot`。
**規格版本**：`interface-spec.md` **v1.5**（驗證順序見 §2.5，該節於 v1.4、v1.5 連改兩次）

> 第 5 步會在 `lerobot` 環境執行，那裡是 **Python 3.12.16**（F-50），時間戳必須走 `interface-spec.md` §2.4 的 ctypes 版本，不能用 `time.time_ns()`。

---

## 一句話總結

<!-- 這一課最核心的一件事。複習時只看這行也要能想起全貌——寫學到什麼，不是做了什麼 -->


---

## 一、struct.unpack 與格式字串

### 用法

<!-- unpack / unpack_from / calcsize；< 的意義；H B I Q i s x 各對應什麼 -->

### 我的理解

<!-- 為什麼 < 不能省？省了在 x86 上為什麼不會出錯？3s 和 3x 差在哪？i 和 I？ -->

### 實驗與結果

### 踩過的坑

---

## 二、切片與長度

### 用法

### 我的理解

<!-- 對 22 bytes 的 data 取 data[275:277] 會怎樣？跟 C 的 buf[275] 差在哪？ -->

### 實驗與結果

### 踩過的坑

---

## 三、驗證順序（interface-spec §2.5）

### 我的理解

<!-- 引導問題（依 v1.5；v1.4 曾有的「連線狀態 / 能否執行」兩層模型已撤銷，不要照那個想）
  1. 為什麼長度檢查要排在 CRC 之前？
  2. 為什麼 payload_len 與 cmd_type 的比對只能排在 CRC 之後？CRC 為什麼擋不住？
  3. 規則是「五項全過才重置 watchdog 並推進 seq 基準」。v1.4 曾放寬成「通過前三項就重置」，
     為什麼那樣會讓 §6.2 的死鎖從第 4 項回來？（提示：B 升級加新 cmd_type 後重啟）
  4. POSE 和急停期間的 JOINT 為什麼算「通過驗證」？它們被拒絕了，為什麼還重置 watchdog？
  5. STALE 為什麼不重置 watchdog？拿掉這個例外會發生什麼事？
  6. UNKNOWN_CMD 和 NOT_IMPLEMENTED 差在哪？為什麼不共用一個碼？
-->

### 實驗與結果

| # | 送入的封包 | 預期 | 實際 |
|---|---|---|---|
| 1 | §2.3 NOP 範例（22 bytes） | 通過，seq = 1 | |
| 2 | | | |

### 踩過的坑

---

## 四、例外的比對順序（F-48）

### 規則

**`except TimeoutError` 必須寫在 `except OSError` 之前。**

`TimeoutError` 是 `OSError` 的子類別。Python 由上往下比對 `except`，第一個符合的就執行、後面的不再比對。所以 `except OSError` 若寫在前面，逾時也會被它接走，`except TimeoutError` 永遠不會被執行。

```python
# 錯：逾時被當成封包錯誤處理
try:
    data, addr = sock.recvfrom(512)
except OSError:
    ...
except TimeoutError:    # 永遠到不了這裡
    continue

# 對：特定的在前，一般的在後
try:
    data, addr = sock.recvfrom(512)
except TimeoutError:
    continue
except OSError:
    ...
```

### 為什麼現在會碰到

第 1 步只接 `TimeoutError`。照 F-33，安全層要接住 `OSError`（`WinError 10040`，封包大於 bufsize），兩個 `except` 一出現在同一個 `try` 後面，順序就有意義了。

寫錯的後果不會報錯：每秒一次的逾時被當成「收到壞封包」處理，記錄與計數會灌水，而且程式看起來照常運作。

### 實驗與結果

<!-- 在 REPL 自己確認並貼上結果 -->

```python
>>> issubclass(TimeoutError, OSError)

>>> TimeoutError.__mro__

```

### 我的理解

<!-- 用自己的話：為什麼特定的要在前？跟 C 的 switch/if-else 鏈有什麼對應？ -->

---

## Python ↔ C 對照

| Python | C | 說明 |
|---|---|---|
| `struct.unpack("<H", data[0:2])` | | |
| `data[275:277]`（超出範圍） | `buf[275]` | |
| `except` 由上往下比對，特定的在前 | `if / else if` 由上往下，範圍小的條件在前 | 先符合者勝 |

---

## 錯誤速查表

| 錯誤訊息 | 原因 | 怎麼修 |
|---|---|---|
| `struct.error: unpack requires a buffer of 20 bytes` | `unpack` 的輸入長度與格式不完全相等（太長也會錯） | 先切片，或改用 `unpack_from` |
| 逾時被當成錯誤處理（不報錯） | `except OSError` 寫在 `except TimeoutError` 之前（F-48） | `TimeoutError` 放前面 |

---

## 對應到專題的設計決策

- 因為 `TimeoutError` 是 `OSError` 的子類別，所以接收迴圈的 `except TimeoutError` 必須在 `except OSError` 之前（F-48）
- 因為 CRC 的涵蓋範圍由 `payload_len` 決定，所以長度檢查排在 CRC 之前（§2.5、F-47）
- 因為 `int()` 向零截斷，所以測試用的發送端換算角度用 `round()`（§7、F-46）

---

## 自我檢查

**1. 不加 `<` 的格式字串，在你的電腦上為什麼測不出錯？**
<details><summary>答案</summary>

</details>

**2. `JOINT` 封包帶 `payload_len = 4`、CRC 正確，會在 §2.5 第幾項被擋？為什麼 CRC 擋不住？**
<details><summary>答案</summary>

</details>

**3. 接收迴圈把 `except OSError` 寫在 `except TimeoutError` 前面，程式會當掉嗎？會發生什麼事？**
<details><summary>答案</summary>

</details>

**4. （自己出一題）**
<details><summary>答案</summary>

</details>
