/*
 * hal_sim.c — hal_ops_t 的模擬器實作，契約見 include/sts_hal.h
 */
#include "hal_sim.h"

#include <stdbool.h>
#include <string.h>

/* now 是否已到達 at；用差值判斷，計數器溢位時仍正確 */
static bool reached(uint32_t now, uint32_t at)
{
    return (uint32_t)(now - at) < 0x80000000u;
}

static void rx_push(hal_sim_t *hs, const uint8_t *buf, size_t n, uint32_t at)
{
    /* 序列線上 byte 不會超車：比前一個 byte 早到的，延到跟它同時抵達 */
    if (hs->rx_len > 0 && !reached(at, hs->rx_at[hs->rx_len - 1]))
        at = hs->rx_at[hs->rx_len - 1];

    for (size_t i = 0; i < n && hs->rx_len < HAL_SIM_RX_CAP; i++) {   /* 滿了就丟，同 UART overrun */
        hs->rx[hs->rx_len]    = buf[i];
        hs->rx_at[hs->rx_len] = at;
        hs->rx_len++;
    }
}

static void rx_pop(hal_sim_t *hs, size_t n)
{
    memmove(hs->rx, hs->rx + n, hs->rx_len - n);
    memmove(hs->rx_at, hs->rx_at + n, (hs->rx_len - n) * sizeof hs->rx_at[0]);
    hs->rx_len -= n;
}

/* 佇列開頭有幾個 byte 已經抵達 */
static size_t rx_ready(const hal_sim_t *hs)
{
    size_t n = 0;
    while (n < hs->rx_len && reached(hs->now_ms, hs->rx_at[n])) n++;
    return n;
}

static int hs_write(void *ctx, const uint8_t *buf, size_t len)
{
    hal_sim_t *hs = ctx;
    uint8_t resp[HAL_SIM_RX_CAP];

    hs->tx_count++;
    hs->tx_last_len = len < HAL_SIM_TX_CAP ? len : HAL_SIM_TX_CAP;
    if (hs->tx_last_len) memcpy(hs->tx_last, buf, hs->tx_last_len);

    size_t n = sim_receive(hs->bus, buf, len, resp, sizeof resp);
    rx_push(hs, resp, n, hs->now_ms + hs->latency_ms);
    return (int)len;
}

static int hs_read(void *ctx, uint8_t *buf, size_t cap)
{
    hal_sim_t *hs = ctx;
    size_t n = rx_ready(hs);
    if (n > cap) n = cap;
    if (hs->rx_chunk && n > hs->rx_chunk) n = hs->rx_chunk;

    if (n == 0) {
        hal_sim_advance(hs, 1);     /* 讀不到就讓時間前進，見 hal_sim.h */
        return 0;
    }
    memcpy(buf, hs->rx, n);
    rx_pop(hs, n);
    return (int)n;
}

static void hs_flush_rx(void *ctx)
{
    hal_sim_t *hs = ctx;
    rx_pop(hs, rx_ready(hs));       /* 還在路上的 byte 清不掉，之後照樣會到 */
}

static uint32_t hs_millis(void *ctx)
{
    const hal_sim_t *hs = ctx;
    return hs->now_ms;
}

void hal_sim_init(hal_sim_t *hs, sim_bus_t *bus)
{
    memset(hs, 0, sizeof *hs);
    hs->bus = bus;
}

const hal_ops_t hal_sim_ops = {
    .write    = hs_write,
    .read     = hs_read,
    .flush_rx = hs_flush_rx,
    .millis   = hs_millis,
};

void hal_sim_advance(hal_sim_t *hs, uint32_t ms)
{
    hs->now_ms += ms;
    sim_tick(hs->bus, ms);
}
