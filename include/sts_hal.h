/*
 * sts_hal.h — 序列埠抽象層（HAL）介面
 *
 * Protocol / Device / Application 三層只透過這組函式指標碰硬體。
 * PC（hal_pc.c）、STM32、模擬器（sim/hal_sim.c）各自實作同一組，
 * 上層一行都不必改。
 *
 * 設計決定（改之前先想清楚會不會動到 Protocol 層）：
 *
 *   1. read 非阻塞：有多少回多少，沒有就回 0。
 *      逾時判斷統一放 Protocol 層，用 millis() 計時，只寫一次、三種 HAL 共用。
 *      STM32 以中斷 / DMA 收進 ring buffer，本來就是非阻塞的形狀。
 *
 *   2. write 回傳 = bytes 已全部送上線，bus 已切回接收方向。
 *      半雙工方向切換是 HAL 的責任，上層不知情：
 *        PC     轉接板自動切換；write 須等到真的送完（tcdrain / FlushFileBuffers）
 *        STM32  等 TC 旗標再切方向腳
 *        sim    把 bytes 交給 servo_sim，回應放進 RX 佇列
 *
 *   3. 時間是 uint32_t 毫秒，比較一律用 hal_timed_out()（即
 *      (uint32_t)(now - start) >= timeout），計數器溢位時仍正確。
 *
 *   4. 所有狀態放在 ctx 指向的 struct，由呼叫者配置；Protocol 層零全域變數。
 *      leader 與 follower 是兩條 bus，同一份程式碼要能同時開兩個實例。
 */
#ifndef STS_HAL_H
#define STS_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_ops {
    /*
     * 送出 buf[0..len)，回傳時 bytes 已全部送上線、bus 可開始接收（決定 2）。
     * 回傳 0 = 成功；負值 = HAL 錯誤（埠被拔掉等），Protocol 層轉成 COMM_TX_FAIL。
     * 不允許只送一部分就回傳。
     */
    int (*write)(void *ctx, const uint8_t *buf, size_t len);

    /*
     * 非阻塞讀取：把目前已收到的資料搬進 buf，最多 cap bytes（決定 1）。
     * 回傳實際讀到的 byte 數（可為 0）；負值 = HAL 錯誤，Protocol 層轉成 COMM_RX_FAIL。
     *
     * 只回傳「對方送來的」bytes。若轉接板會把自己的 TX 回送到 RX（echo），
     * 由 HAL 吃掉——echo 的指令包格式剛好也能通過 parser 的合理性檢查
     * （例：PING 的 Instruction 0x01 會被當成 error byte），上層無從分辨。
     * TODO(W10)：真機確認轉接板有沒有 echo（LeRobot-notes 第 9 節）。
     */
    int (*read)(void *ctx, uint8_t *buf, size_t cap);

    /*
     * 丟棄所有已收到、尚未讀取的資料（含 OS / 驅動層的緩衝區）。
     * Protocol 層在每次送指令前呼叫，避免把上一包的殘留當成這一包的回應。
     */
    void (*flush_rx)(void *ctx);

    /*
     * 單調遞增的毫秒計數，起點不重要，溢位後從 0 繼續（決定 3）。
     * 模擬器回傳虛擬時間；上層的等待迴圈每圈都要呼叫 read 與 millis，
     * 不可用其他方式空等，否則虛擬時鐘不會前進。
     *
     * TODO(W12)：延遲量測需要 µs 解析度。在 HAL 加 micros，
     *            或只在量測程式裡用 OS 計時器？
     */
    uint32_t (*millis)(void *ctx);

    /* 各實作自己的狀態（序列埠 handle、ring buffer、模擬器實例…），上層不解讀 */
    void *ctx;
} hal_ops_t;

/* 從 start 起是否已經過 timeout_ms 毫秒。now 溢位繞回 0 之後仍正確（決定 3）。 */
static inline bool hal_timed_out(uint32_t start, uint32_t now, uint32_t timeout_ms)
{
    return (uint32_t)(now - start) >= timeout_ms;
}

#ifdef __cplusplus
}
#endif

#endif /* STS_HAL_H */
