/*
 * sts_device.h — Device 層：暫存器數值的編碼
 *
 * STS3215 的多位元組數值有兩個要注意的地方（docs/LeRobot-notes.md 4.1）：
 *
 *   1. byte 順序是 little-endian：低位元組在前。
 *      例：位置 1304 = 0x0518 → 線上是 18 05。
 *
 *   2. 有號數用 sign-magnitude，不是二補數：最高的「方向位」代表正負，
 *      其餘位元是絕對值。方向位的位置依暫存器而不同：
 *        位置 / 速度   BIT15
 *        負載          BIT10
 *        Homing Offset BIT11
 *      例：−100 寫入 Goal_Position → 0x8000 | 100 = 0x8064 → 線上是 64 80。
 *      若誤用二補數會送出 0xFF9C，馬達會解讀成另一個值。
 *
 * 暫存器位址表之後也放在這一層。
 */
#ifndef STS_DEVICE_H
#define STS_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 各暫存器的方向位位置 */
#define STS_SIGN_BIT_POSITION  15   /* Goal / Present Position */
#define STS_SIGN_BIT_SPEED     15   /* Goal / Present Speed */
#define STS_SIGN_BIT_LOAD      10   /* Present Load */
#define STS_SIGN_BIT_OFFSET    11   /* Homing Offset */

/*
 * 有號整數 → sign-magnitude。sign_bit 是方向位的位置（1–15）。
 * 絕對值超過方向位以下能表示的範圍時，截在最大值（例：BIT15 時為 ±32767）。
 * 截斷而不是讓高位溢進方向位，避免送出方向相反的指令。
 */
uint16_t sts_sm_encode(int32_t value, unsigned sign_bit);

/* sign-magnitude → 有號整數。方向位以上的位元忽略；「負零」解成 0。 */
int32_t sts_sm_decode(uint16_t raw, unsigned sign_bit);

/* little-endian：p[0] 是低位元組、p[1] 是高位元組 */
uint16_t sts_le16_get(const uint8_t *p);
void     sts_le16_put(uint8_t *p, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* STS_DEVICE_H */
