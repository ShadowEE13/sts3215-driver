/*
 * hal_sim.h — 假的 HAL：hal_ops_t 接到 servo_sim，時間是虛擬的
 *
 *   sim_bus_t bus;   sim_init(&bus);   sim_add_servo(&bus, 1);
 *   hal_sim_t hs;    hal_sim_init(&hs, &bus);
 *   hal_ops_t hal = hal_sim_ops(&hs);      // 交給 Protocol 層
 *
 * 虛擬時鐘：millis() 回傳 now_ms，不碰真實時間。
 *   - read 讀不到資料時前進 1 ms（否則「馬達不回應」時 Protocol 層的
 *     while (!逾時) read() 會變成無窮迴圈）
 *   - 測試可用 hal_sim_advance() 一次跳過一段時間
 * 時間每前進一次，運動模型就跟著 tick。逾時測試因此是確定性的，且瞬間跑完。
 */
#ifndef HAL_SIM_H
#define HAL_SIM_H

#include <stddef.h>
#include <stdint.h>

#include "servo_sim.h"
#include "sts_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_SIM_RX_CAP 1024
#define HAL_SIM_TX_CAP 300     /* 最長封包 4 + 255，多留一點 */

typedef struct {
    sim_bus_t *bus;
    uint32_t   now_ms;

    /* 測試可調的參數（hal_sim_init 後皆為 0） */
    uint32_t latency_ms;   /* 回應在 write 之後多久才讀得到：測逾時邊界、遲到的舊回應 */
    size_t   rx_chunk;     /* 每次 read 最多回幾 byte，0 = 不限：測半包 */

    /* 給測試檢查的紀錄 */
    uint32_t tx_count;                 /* write 被呼叫的次數：測重傳次數 */
    uint8_t  tx_last[HAL_SIM_TX_CAP];  /* 最後一次 write 的內容（超過截斷） */
    size_t   tx_last_len;

    /* RX 佇列：每個 byte 記錄抵達時間，未抵達的 read 不到、flush 也清不掉 */
    uint8_t  rx[HAL_SIM_RX_CAP];
    uint32_t rx_at[HAL_SIM_RX_CAP];
    size_t   rx_len;
} hal_sim_t;

void      hal_sim_init(hal_sim_t *hs, sim_bus_t *bus);
hal_ops_t hal_sim_ops(hal_sim_t *hs);

/* 虛擬時間前進 ms 毫秒，運動模型同步 tick */
void hal_sim_advance(hal_sim_t *hs, uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SIM_H */
