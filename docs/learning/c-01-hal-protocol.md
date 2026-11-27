# C 01：HAL 介面與協定編碼

**日期**：2026/11/04（W9）
**對應**：C 線步驟 1（`include/sts_hal.h`）與步驟 5（`src/sts_protocol.c`、`src/sts_device.c`）
**環境**：Windows、MSYS2 UCRT64（見 `docs/dev-setup.md`）

> 本篇整理自 W9 review 時的理解確認題。程式由 Claude Code 撰寫（C-13），這份筆記記錄的是我讀懂了什麼、第一次答錯在哪。下學期接手 C 線（步驟 6 起）前先複習。

---

## 一句話總結

<!-- 這一課最核心的一件事。複習時只看這行也要能想起全貌——寫學到什麼，不是做了什麼 -->


---

## 一、函式表與 `ctx`

**在哪裡**：`include/sts_hal.h` 的 `hal_ops_t`、`sts_hal_t`；設計決策 4、5（`../conventions.md` §4）

**重點**
- 函式表裡只有程式、沒有狀態；狀態全部在 `ctx`
- 每次呼叫都帶上那條 bus 自己的 `ctx`：`hal_write(&leader, ...)` 最後執行的是 `uart_write(&leader_port, ...)`
- 同一個函式，傳入不同的 `ctx`，就操作不同的硬體。Linux kernel 的 `struct file_operations` 是同一個模式

### 我的理解


---

## 二、計數器溢位與 `hal_timed_out()`

**在哪裡**：`include/sts_hal.h` 的 `hal_timed_out()`；`tests/test_hal.c` 的溢位對照

**重點**：`start = 0xFFFFFFF0`、`timeout = 100` 時，直覺寫法 `now >= start + timeout` 的推演：

```
start + timeout = 0xFFFFFFF0 + 100 = 0x1_0000_0054
                → 超過 32 bit 被丟掉 → 0x00000054（= 84）
1 ms 後 now = 0xFFFFFFF1 ≥ 84 → 成立 → 誤判逾時
```

才過 1 ms 就被判定超過 100 ms。改成 `(uint32_t)(now - start) >= timeout`，`now - start = 1`，判斷正確。

32 bit 毫秒計數器約每 **49.7 天**溢位一次：開發時跑幾小時都不會遇到，產品在現場連續運作一個多月才突然出錯。早期 Windows 95/98 連續開機約 49.7 天會當機，就是這類計時器溢位問題。

### 我的理解


---

## 三、echo 為什麼必須由 HAL 濾掉

**在哪裡**：`include/sts_hal.h` 的 `read` 註解；F-20、V-06

**重點**：echo 回來的指令包，在 parser 眼中是一個完全合法的回應包。

```
送出的 PING：  FF FF  01   02   01    FB
                     ID  Len  Inst  Checksum
當成回應解讀：FF FF  01   02   01    FB
                     ID  Len  Error Checksum   ← Error = 0x01（電壓異常）
```

字頭、ID、Length、checksum 全部合法，Error 0x01 < 0x7F 也通過合理性檢查。驅動會回報「1 號馬達電壓異常」，真正的回應晚一步到、反而被當垃圾丟掉。

Protocol 層光看 bytes 分不出來。只有 HAL 知道「我剛送出了什麼」與「這片轉接板會不會 echo」；而且 echo 是硬體特性（每片轉接板不同，STM32 單線半雙工模式本來就會收到自己送的資料），屬於 HAL 該吸收的差異。

### 我的理解


---

## 四、checksum 的累加型別

**在哪裡**：`src/sts_protocol.c` 的 `sts_checksum()`

**重點**：用 `uint8_t` 累加、每一步都溢位繞回，算出的 checksum **完全一樣**。加法結果的低 8 位元只取決於兩數的低 8 位元，而 checksum 只取最低 8 位元。程式用 `uint32_t` 累加的理由見原始碼註解：在 `int` 只有 16 bit 的 MCU 上，用 `int` 累加 253 個 byte 可能超過 32767，有號溢位是未定義行為。改用 `uint8_t` 累加結果一樣正確，只是每一步都要明確轉型才過得了 `-Wconversion`。

### 我的理解


---

## 五、sign-magnitude 誤用二補數的後果

**在哪裡**：`src/sts_device.c`；F-01

**重點**：−100 誤用二補數送出 `9C FF`，馬達以 sign-magnitude 解讀 `0xFF9C`：方向位 = 1，其餘 `0x7F9C` = 32668 → **−32668**。想轉 100 步，結果反方向轉三萬多步。

另見 `sts_sm_encode` 的已知限制：超範圍時靜默截斷，安全層須在編碼前擋下（`week06-08-plan.md` 驗收 B 之後）。

### 我的理解


---

## 六、反向驗證（mutation testing）

**在哪裡**：`week06-08-plan.md` 驗收 B；`lab-log/week09a.md` 第三節

**重點**：一個永遠不會失敗的測試，什麼也證明不了。故意把 checksum 改成最擔心的那種錯（連 `FF FF` 一起加），驗收 B 有 4 項失敗，證明測試對這類錯誤有鑑別力。這個做法的名稱是 **mutation testing**。

### 我的理解


---

## 自我檢查

**1. leader 和 follower 共用同一張 `hal_ops_t`，為什麼兩條 bus 的資料不會混在一起？**
<details><summary>答案</summary>

（第一次答「`hal_ops_t` 只規定長怎樣，兩條 bus 仍然是獨立的」，大致正確，但沒說出關鍵字。）函式表只有程式沒有狀態，狀態全在 `ctx`；每次呼叫都帶上那條 bus 自己的 `ctx`，所以同一個函式操作的是不同的 port。
</details>

**2. `start = 0xFFFFFFF0`、`timeout = 100`，用 `now >= start + timeout`，過 1 ms 時會怎樣？**
<details><summary>答案</summary>

（第一次只答「會溢位」——那是原因，沒說後果。）`start + timeout` 溢位成 84，`0xFFFFFFF1 ≥ 84` 成立，才過 1 ms 就誤判逾時。約每 49.7 天才觸發一次，所以開發時測不到。見第二節。
</details>

**3. 為什麼 echo 一定要在 HAL 濾掉？留給 Protocol 層會出什麼問題？**
<details><summary>答案</summary>

（第一次答「Protocol 層不知道到底有沒有 echo」，方向對，但沒說出為什麼分不出來。）PING 的 echo 在 parser 眼中是合法回應：字頭、ID、Length、checksum 全對，Instruction 0x01 被當成 error = 電壓異常。光看 bytes 無法分辨；只有 HAL 知道自己剛送了什麼、轉接板會不會 echo。見第三節。
</details>

**4. `sts_checksum` 改用 `uint8_t` 累加，每次相加都溢位，算出的 checksum 會錯嗎？**
<details><summary>答案</summary>

（第一次就答對。）不會錯，完全一樣。checksum 只要最低 8 位元，加法結果的低 8 位元只取決於兩數的低 8 位元。
</details>

**5. −100 誤用二補數送出 `9C FF`，馬達會當成多少？**
<details><summary>答案</summary>

（第一次就答對。）−32668。方向位 1、其餘 `0x7F9C` = 32668。
</details>

**6. 故意把 checksum 改壞、確認驗收 B 會失敗，這個反向驗證證明了什麼？**
<details><summary>答案</summary>

（第一次答「證明驗收 B 不是單純接收就 ok」，方向對。）證明測試**對這類錯誤有鑑別力**：一個永遠不會失敗的測試什麼也證明不了。這種做法叫 mutation testing。
</details>

**7. （自己出一題）**
<details><summary>答案</summary>

</details>
