/*
 * sts_hal.h — 序列埠抽象層（HAL）介面
 *
 * Protocol / Device / Application 三層只透過這個介面碰硬體。
 * PC（W10 的 src/hal_win32.c）、STM32、模擬器（sim/hal_sim.c）各自實作同一組函式，
 * 上層一行都不必改。
 *
 * 用法：
 *
 *     static const hal_ops_t uart_ops = { uart_write, uart_read, uart_flush_rx, uart_millis };
 *     sts_hal_t leader   = { &uart_ops, &leader_port };    // 同一張函式表
 *     sts_hal_t follower = { &uart_ops, &follower_port };  // 不同的 ctx
 *     hal_write(&leader, pkt, len);
 *
 * 設計決定（改之前先想清楚會不會動到 Protocol 層；完整紀錄見 CLAUDE.md）：
 *
 *   1. read 非阻塞：有多少回多少，沒有就回 0。
 *      逾時判斷統一放 Protocol 層，用 millis() 計時——只寫一次，三種 HAL 共用。
 *      STM32 以中斷 / DMA 收進 ring buffer，本來就是非阻塞的形狀。
 *
 *   2. write 回傳時，bytes 已全部送上線，bus 可以開始接收。
 *      半雙工方向切換是 HAL 實作的責任，上層不知情：
 *        PC     轉接板自動切換；write 要等資料真的送完才回傳
 *        STM32  等 UART 的 TC（傳輸完成）旗標，再切方向腳
 *        sim    把 bytes 交給 servo_sim，回應放進 RX 佇列
 *
 *   3. 時間是 uint32_t 毫秒，由 millis() 提供。比較一律用 hal_timed_out()，
 *      也就是 (uint32_t)(now - start) >= timeout，計數器溢位時仍正確。
 *
 *   4. 每個函式的第一個參數都是 void *ctx，指向實作自己的狀態（序列埠 handle、
 *      ring buffer、模擬器實例…）。Protocol 層只轉交、不解讀，所以不需要任何
 *      全域變數，leader 與 follower 兩條 bus 可以同時存在。
 *
 *   5. 函式表與實例分開：hal_ops_t 只放函式指標，宣告成 static const；
 *      sts_hal_t = 函式表指標 + ctx，代表「一條 bus」。
 *      const 的函式表在 STM32 上會放進 flash、不佔 RAM；leader 與 follower
 *      共用同一張表，各帶自己的 ctx。Linux kernel 的 struct file_operations
 *      也是這個模式。
 *
 *   6. write / read 回傳 int（實際送出 / 讀到的 byte 數，< 0 代表錯誤），
 *      長度參數用 size_t。回傳時把 size_t 轉成 int 是安全的，前提是單次長度很小：
 *      封包的 Length 欄位只有 1 byte，整包最長 4 + 255 = 259 bytes
 *      （SDK 的合理性檢查更限制 Length ≤ 250），呼叫者的 buffer 也只有幾百 bytes，
 *      遠小於 INT_MAX。
 */
#ifndef STS_HAL_H
#define STS_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 函式表：一種硬體一張，宣告成 static const（決定 5） */
typedef struct hal_ops {
    /*
     * 送出 buf[0..len)，回傳時 bytes 已全部送上線、bus 可開始接收（決定 2）。
     * 回傳實際送出的 byte 數，正常一定等於 len；< 0 = 錯誤（例如埠被拔掉）。
     * Protocol 層把「< 0 或 != len」都當成送出失敗（COMM_TX_FAIL）。
     */
    int (*write)(void *ctx, const uint8_t *buf, size_t len);

    /*
     * 非阻塞讀取：把目前已收到的資料搬進 buf，最多 cap bytes（決定 1）。
     * 回傳實際讀到的 byte 數，沒有資料就回 0；< 0 = 錯誤（COMM_RX_FAIL）。
     *
     * 只回傳「對方送來的」bytes：若轉接板會把自己的 TX 回送到 RX（echo），
     * 由 HAL 吃掉。echo 回來的指令包剛好也能通過 parser 的檢查
     * （例：PING 的 Instruction 0x01 會被當成 error byte），上層無從分辨。
     * TODO(W10)：真機確認轉接板有沒有 echo（LeRobot-notes 第 9 節）。
     */
    int (*read)(void *ctx, uint8_t *buf, size_t cap);

    /*
     * 丟棄所有已收到、還沒讀走的資料。
     * Protocol 層在每次送指令前呼叫，避免把上一包的殘留當成這一包的回應。
     */
    void (*flush_rx)(void *ctx);

    /*
     * 單調遞增的毫秒計數，起點不重要，溢位後從 0 繼續（決定 3）。
     * 模擬器回傳虛擬時間，所以上層的等待迴圈每一圈都要呼叫 read 與 millis，
     * 不可用其他方式空等，否則虛擬時鐘不會前進。
     * TODO(W12)：延遲量測需要 µs 解析度——在這裡加 micros，還是只在量測程式用 OS 計時器？
     */
    uint32_t (*millis)(void *ctx);
} hal_ops_t;

/* 一條 bus 的實例：用哪一張函式表 + 這條 bus 自己的狀態（決定 4、5） */
typedef struct sts_hal {
    const hal_ops_t *ops;
    void            *ctx;
} sts_hal_t;

/*
 * 呼叫用的小包裝：替呼叫者把 hal->ctx 轉交給函式表，
 * Protocol 層寫 hal_write(hal, ...) 就好，不必每次寫 hal->ops->write(hal->ctx, ...)。
 */
static inline int hal_write(const sts_hal_t *hal, const uint8_t *buf, size_t len)
{
    return hal->ops->write(hal->ctx, buf, len);
}

static inline int hal_read(const sts_hal_t *hal, uint8_t *buf, size_t cap)
{
    return hal->ops->read(hal->ctx, buf, cap);
}

static inline void hal_flush_rx(const sts_hal_t *hal)
{
    hal->ops->flush_rx(hal->ctx);
}

static inline uint32_t hal_millis(const sts_hal_t *hal)
{
    return hal->ops->millis(hal->ctx);
}

/*
 * 從 start 起是否已經過 timeout_ms 毫秒（決定 3）。
 * 用減法而不是 now >= start + timeout：start + timeout 可能溢位繞回 0，
 * 而無號數相減在溢位後仍然得到正確的經過時間。
 */
static inline bool hal_timed_out(uint32_t start, uint32_t now, uint32_t timeout_ms)
{
    return (uint32_t)(now - start) >= timeout_ms;
}

#ifdef __cplusplus
}
#endif

#endif /* STS_HAL_H */
