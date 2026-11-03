/*
 * sts_device.c — sign-magnitude 與 little-endian 編解碼
 */
#include "sts_device.h"

uint16_t sts_sm_encode(int32_t value, unsigned sign_bit)
{
    uint32_t max = (1u << sign_bit) - 1u;     /* 方向位以下全部是 1，例：BIT15 → 0x7FFF */

    /*
     * 取絕對值。不寫 -value：value 為 INT32_MIN 時 -value 會溢位（未定義行為）。
     * 先轉成 uint32_t 再用 0 減，無號數的減法有明確定義。
     */
    uint32_t mag = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (mag > max) mag = max;

    uint32_t raw = value < 0 ? (mag | (1u << sign_bit)) : mag;
    return (uint16_t)raw;
}

int32_t sts_sm_decode(uint16_t raw, unsigned sign_bit)
{
    uint32_t r   = raw;                       /* 先轉 unsigned，避免 int 提升後的有號/無號混用 */
    uint32_t max = (1u << sign_bit) - 1u;
    int32_t  mag = (int32_t)(r & max);        /* ≤ 32767，轉 int32_t 安全 */

    return ((r >> sign_bit) & 1u) ? -mag : mag;
}

uint16_t sts_le16_get(const uint8_t *p)
{
    /* 位元運算前 uint8_t 會被提升成 int，結果要明確轉回 uint16_t（-Wconversion） */
    return (uint16_t)(p[0] | (p[1] << 8));
}

void sts_le16_put(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value & 0xFFu);          /* 低位元組 */
    p[1] = (uint8_t)(value >> 8);             /* 高位元組 */
}
