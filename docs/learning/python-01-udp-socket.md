# Python 01：UDP socket

**日期**：2026/11/17（W11）
**對應**：Python 第 1 步（`pyctrl/receiver.py`）
**環境**：Windows、Miniforge、`lerobot` 環境（Python ≥ 3.13）

---

## 一句話總結

<!-- 這一課最核心的一件事。複習時只看這行也要能想起全貌 -->
學習socket模組的使用方式，自己建立Reciever。

---

## 一、建立 socket

### 用法

```python
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
...
sock.close()
```

| 參數 | 回答的問題 | 選擇 | 其他選項 |
|---|---|---|---|
| `AF_INET` | 位址長什麼樣子 | IPv4（`127.0.0.1`） | `AF_INET6` |
| `SOCK_DGRAM` | 資料怎麼送 | 一包一包（UDP） | `SOCK_STREAM`（TCP，連續 byte 流） |

### 我的理解

<!-- socket 是什麼？兩個參數各回答什麼問題？和 serial.Serial("COM5") 差在哪？ -->
- socket就像是信箱
- AF_INET代表使用的是IPv4
- SOCK_DGRAM代表使用的是UDP
- serial.Serial("COM5") 在建立的那一刻就接上了裝置；socket 剛建立時還沒有位址，要等 bind 之後才有。

### 實驗與結果

```python
>>> sock
<socket.socket fd=580, family=2, type=2, proto=0>
>>> sock.getsockname()
OSError: [WinError 10022] 提供了一個不正確的引數。
>>> sock.close()
>>> sock
<socket.socket [closed] fd=-1, family=2, type=2, proto=0>
```

- `family=2`、`type=2`：`AF_INET` 與 `SOCK_DGRAM` 本質是整數常數，值與 C 的 `<sys/socket.h>` 相同
- `fd=580`：作業系統配給這個 socket 的編號（Windows 稱 handle）
- `getsockname()` 失敗：還沒 `bind`，沒有位址。Linux 上同樣操作不報錯，回傳 `('0.0.0.0', 0)`
- `close()` 之後 `fd=-1`：資源已歸還，物件還在但不能用

### 踩過的坑

| 寫法 | 結果 | 原因 |
|---|---|---|
| `socket.getsockname()` | `AttributeError: module 'socket' has no attribute 'getsockname'` | `socket` 是模組（工具箱），`sock` 才是物件（信箱），方法要對物件呼叫 |
| `sock.getsockname` | `<built-in method getsockname ...>` | 沒加 `()` 只是指向方法，不會執行 |

---

## 二、bind

### 用法

```python
sock.bind(("127.0.0.1", 49200))   # 兩層括號：一個參數，內容是 (ip, port) 的 tuple
```

- `127.0.0.1`：只收本機送來的封包；`0.0.0.0` 會讓 Windows 防火牆跳警告
- `49200`：落在 IANA 動態／私有範圍 49152–65535

### 我的理解

<!-- 為什麼接收端要 bind、發送端不用？127.0.0.1 vs 0.0.0.0？port 範圍？ -->
- 接受端要bind，才可以讓發送端知道要送給誰。發送端就交給系統自己配(不影響)
- 127.0.0.1就是本機自己
- 0.0.0.0會讓 Windows 防火牆跳警告
- port 範圍從0 ~ 65535， IANA 動態／私有範圍 49152 ~ 65535


### 實驗與結果

| # | 實驗 | 結果 |
|---|---|---|
| 1 | bind 後問門牌 | `('127.0.0.1', 49200)` |
| 2 | 另一個視窗 bind 同一個 port | `OSError: [WinError 10048] 一次只能用一個通訊端位址` |
| 3 | bind 到 port 0 | `('127.0.0.1', 57285)`：作業系統從動態範圍挑一個空號 |
| 4 | `bind("127.0.0.1", 49201)`（少一層括號） | `TypeError: socket.bind() takes exactly one argument (2 given)` |

實驗 3 就是發送端沒有 `bind` 時，作業系統在第一次 `sendto` 自動做的事。

### 踩過的坑

- 少一層括號 → `TypeError`（實驗 4）
- 同一個 port 只能有一個 socket；receiver 開兩次、或上一個沒關乾淨 → `WinError 10048`

---

## 三、recvfrom

### 用法

```python
sock.settimeout(1)                  # 最多等 1 秒，逾時拋 TimeoutError
data, addr = sock.recvfrom(512)     # 512 = interface-spec §1 的封包上限
```

- 回傳 `(data, addr)`：`data` 是 `bytes`，`addr` 是發送端的 `(ip, port)`
- 預設**阻塞**：沒有封包就一直等
- 發送端用 `sendto(data, (ip, port))`，不需要 `bind`

### 我的理解

<!-- 回傳什麼？阻塞是什麼意思？UDP 和串流（序列埠／TCP）的根本差別？ -->
- 回傳收到的資料與地址
- 阻塞就是在還沒收到前，繼續等下去
- UDP 會保留每一包的邊界；串流是連續的 byte，邊界要自己切

### 實驗與結果

| # | 實驗 | 結果 |
|---|---|---|
| 1 | B 送 `b"hello"`，A 收 | `(b'hello', ('127.0.0.1', 49966))`：49966 是 B 自動分配到的 port |
| 2 | B 先送 `AAA`、`BBBBB`，A 再收兩次 | 收到兩包 `AAA`、`BBBBB`，沒有黏在一起；先送後收也沒遺失，封包在緩衝區排隊 |
| 3 | B 送 10 bytes，A `recvfrom(4)` | `OSError: [WinError 10040] ... 接收資料包的緩衝區小於資料包本身` |

實驗 3 的意義：UDP 一包是不可分割的單位，沒收完的部分**直接丟棄**，不會留到下次讀（串流則會留著）。Linux 上不報錯，而是靜默截斷。
（未實測：錯誤之後再 `recvfrom(512)`，預期等到 timeout，剩下的 6 bytes 已不在）

### 踩過的坑

- Windows 上阻塞中的 `recvfrom` 按 Ctrl+C 沒反應，要等下一包進來 → 用 `settimeout`
- bufsize 照「預期大小」設（例如 46）會出事 → 照規格上限設

---

## 四、bytes 與 hex

### 用法

```python
data = b"hello"
len(data)         # 5
data[0]           # 104 → int
data[2:4]         # b'll' → bytes
data.hex(" ")     # '68 65 6c 6c 6f'
bytes([0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB])   # 從數字串列建立
```

### 我的理解

<!-- 索引拿到什麼型別？切片呢？ -->
- 索引拿到int；切片拿到bytes


### 實驗與結果

```python
>>> data[0]
104                                   # 'h' 的 ASCII 0x68
>>> pkt = bytes([0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB])
>>> pkt.hex(" ")
'ff ff 01 02 01 fb'                   # STS3215 對 ID 1 的 PING
>>> pkt[2]
1                                     # ID 欄位，跟 C 的 buf[2] 一樣
```

### 踩過的坑

- `data.hex("")` → `ValueError: sep must be length 1`。不要分隔符號就什麼都不傳：`data.hex()`

---

## 五、組起來：receiver.py

### 結構

```python
import socket

ADDR = ("127.0.0.1", 49200)

try:
    ##### Main #####
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(ADDR)
        sock.settimeout(1)

        ##### Loop #####
        while True:
            try:
                data, addr = sock.recvfrom(512)
            except TimeoutError:
                continue
            print(f"{len(data)} bytes from {addr}: {data.hex(' ')}")

except KeyboardInterrupt:
    print("已停止")
```

| 層 | 作用 |
|---|---|
| 外層 `try / except KeyboardInterrupt` | Ctrl+C 可能發生在任何一行，所以包住全部 |
| `with` | 離開區塊時自動 `close()`；Ctrl+C 時先關 socket，再進 `except` |
| `while True` | 持續收包 |
| 內層 `try / except TimeoutError` | 只包 `recvfrom`，逾時就回到迴圈開頭，讓 Ctrl+C 有機會生效 |
| `print` | 在 `while` 裡、內層 `try` 外 |

<!-- 我的理解：用自己的話說每一層為什麼要在那個位置 -->


### 驗收結果

```
46 bytes from ('127.0.0.1', 53335): 43 53 01 01 f6 00 00 00 23 09 f5 da 73 60 06 00 18 00 ... 00 f6 53
46 bytes from ('127.0.0.1', 53335): 43 53 01 01 f7 00 00 00 74 33 f5 da 73 60 06 00 18 00 ... 00 d6 d5
```

| 欄位 | 第一包 | 第二包 |
|---|---|---|
| 長度 | 46 | 46 |
| magic / version / cmd | `43 53` → `0x5343` / 1 / JOINT | 同左 |
| seq | `f6 00 00 00` → 246 | `f7 00 00 00` → 247 |
| timestamp_us | 15:29:31.261219 | 15:29:31.272052 |
| payload_len | `18` → 24（十六進位！） | 24 |
| CRC（重算 vs 收到） | `0x53F6` = `0x53F6` | `0xD5D6` = `0xD5D6` |

- interface-spec v1.2 第一次在 Windows 上被完整執行，CRC 由獨立實作驗算一致
- **觀察**：兩包相差 10.833 ms，不是 `sleep(0.01)` 的 10 ms。多出的時間是組包、CRC、`sendto` 與 Windows `sleep` 的不精確。固定 `sleep` 會累積誤差 → 第 3 步改用 `perf_counter()` 排程

### 踩過的坑

| 問題 | 原因 | 修正 |
|---|---|---|
| `try` 和 `while` 同一層縮排 | Python 的縮排是語法，`while` 本體變成空的 → `IndentationError` | 選取區塊按 `Tab` 往右一層 |
| `print` 放在 `try` 裡 | 第 2 步加了解析後，其他例外可能被誤吞 | `try` 只包 `recvfrom` |
| 縮排 2 格 | PEP 8 建議 4 格 | 全部統一 4 格（Python 與 C 都是） |

---

## Python ↔ C 對照

| Python | C | 說明 |
|---|---|---|
| `socket.socket(AF_INET, SOCK_DGRAM)` | `socket(AF_INET, SOCK_DGRAM, 0)` | 同一套 BSD socket API，lwIP 等嵌入式網路堆疊也是 |
| `bytes` | `uint8_t buf[]` | 0–255 的數字陣列 |
| `data[2]` → `int` | `buf[2]` | 取單一 byte |
| `sock.close` vs `sock.close()` | `f` vs `f()` | 前者是函式本身（指標），後者才是呼叫 |
| `with ... as sock:` | `goto cleanup` | 保證離開時釋放資源 |
| 縮排 | `{ }` | Python 的縮排決定區塊，不只是排版 |
| `try` 範圍 = 例外可能發生的範圍 | 每個可能失敗的呼叫馬上檢查回傳值 | 錯誤處理要貼近錯誤來源 |
| `fd=-1`（已關閉） | 關閉後的 fd 不可再用 | 編號已還給作業系統 |

---

## 錯誤速查表

| 錯誤訊息 | 原因 | 怎麼修 |
|---|---|---|
| `[WinError 10022] 提供了一個不正確的引數` | `WSAEINVAL`：操作在目前狀態不合法（例：未 bind 就 `getsockname()`） | 先 `bind`；中文訊息常誤導，查錯誤碼 |
| `[WinError 10048] 一次只能用一個通訊端位址` | port 已被佔用 | 關掉另一個 receiver；用 `with` 確保 socket 會關 |
| `[WinError 10040] ... 緩衝區小於資料包本身` | `WSAEMSGSIZE`：封包大於 bufsize，超出部分已丟棄 | bufsize 用規格上限；安全層要接住 `OSError` 繼續跑 |
| `TypeError: socket.bind() takes exactly one argument (2 given)` | `bind` 少一層括號 | `bind((ip, port))` |
| `AttributeError: module 'socket' has no attribute ...` | 對模組呼叫物件的方法 | 改成 `sock.xxx()` |
| `ValueError: sep must be length 1` | `hex("")` | `hex()` 或 `hex(" ")` |
| `IndentationError: expected an indented block after 'while'` | 迴圈本體沒有縮排 | 往右縮一層 |

---

## 對應到專題的設計決策

- 因為 UDP 超出 bufsize 的部分會直接丟棄（實驗 10040），所以 `recvfrom` 用規格上限 **512**，不用 JOINT 的 46
- 因為 Windows 上 B 送來過大的封包會拋 `OSError`，所以**第 4 步安全層要接住它並丟掉那一包**，不能讓整個控制層停掉
- 因為阻塞中的 `recvfrom` 按 Ctrl+C 沒反應，所以 `settimeout(1)`
- 因為封包會在緩衝區排隊、`recvfrom` 拿到的是最舊的那包，所以**第 3 步要抽乾佇列只留最新一包**，否則延遲會越拉越大
- 因為 `sleep(0.01)` 實測週期是 10.8 ms，所以**第 3 步用 `perf_counter()` 計算下一個喚醒點**，不用固定 `sleep`
- 因為 UDP 保留封包邊界，所以接收端**不需要找字頭**（對比 `snapshot.py` 收序列埠要找 `FF FF`）

---

## 自我檢查

**1. 為什麼接收端要 `bind`，發送端不用？**
<details><summary>答案</summary>

接收端需要讓系統知道誰的信要給他。接收端的位址必須固定，發送端才知道往哪送；發送端自己的 port 不重要，作業系統在第一次 `sendto` 時自動分配（實驗：port 0 → 57285、hello → 49966）。做狀態回傳（C → B，49201）時反過來，換 B 要 `bind`。
</details>

**2. 序列埠收資料要找 `FF FF` 字頭，為什麼 UDP 接收端不用？**
<details><summary>答案</summary>

UDP 保留封包邊界：一包要嘛整包收到，要嘛整包不見，不會半包、不會黏在一起（實驗：`AAA`、`BBBBB` 分兩次收到）。序列埠與 TCP 是連續的 byte 流，邊界要自己切。注意 UDP 不保證送達。
</details>

**3. 發送端每 1 ms 送一包，接收端每次 `recvfrom` 後 `sleep(0.1)`，會發生什麼事？**
<details><summary>答案</summary>

（第一次答「沒有新的封包可以收」，剛好相反。）封包多到收不完：每秒多出 990 包堆在作業系統緩衝區排隊，`recvfrom` 每次拿最舊的，延遲越來越大；緩衝區滿了之後，作業系統丟掉的是**新進來**的封包。結果是留下舊資料、丟掉新資料，手臂跟著幾秒前的手勢動，而且沒有任何錯誤訊息。解法：第 3 步抽乾佇列只留最新一包。
</details>

**4. （自己出一題）**
<details><summary>答案</summary>

</details>
