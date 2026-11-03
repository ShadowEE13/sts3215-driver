/*
 * test_device.c — 步驟 5：sign-magnitude 與 little-endian 編解碼
 *
 * 預期值來自 docs/LeRobot-notes.md 4.1（−100 → 64 80）與
 * protocol-notes 第 4 節的範例資料（18 05 = 1304、00 08 = 2048、E8 03 = 1000）。
 */
#include <stdint.h>

#include "check.h"
#include "sts_device.h"

static void test_little_endian(void)
{
    /* 手冊 4.2 的回應資料 18 05 → 0x0518 = 1304 */
    static const uint8_t pos[] = {0x18, 0x05};
    CHECK_EQ(sts_le16_get(pos), 1304);

    /* 手冊 4.3 的參數：2048 → 00 08、1000 → E8 03 */
    uint8_t buf[2];
    sts_le16_put(buf, 2048);
    CHECK_BYTES(buf, ((const uint8_t[]){0x00, 0x08}), 2);
    sts_le16_put(buf, 1000);
    CHECK_BYTES(buf, ((const uint8_t[]){0xE8, 0x03}), 2);

    /* 高位元組大於 0x7F 也要正確（不能被當成負數） */
    static const uint8_t high[] = {0x34, 0xF2};
    CHECK_EQ(sts_le16_get(high), 0xF234);
}

static void test_sm_encode(void)
{
    uint8_t buf[2];

    /* F-01：−100（BIT15）→ 0x8064 → 線上 64 80；二補數會是 0xFF9C（錯） */
    CHECK_EQ(sts_sm_encode(-100, STS_SIGN_BIT_POSITION), 0x8064);
    sts_le16_put(buf, sts_sm_encode(-100, STS_SIGN_BIT_POSITION));
    CHECK_BYTES(buf, ((const uint8_t[]){0x64, 0x80}), 2);

    /* 正數與 0：跟一般無號數相同 */
    CHECK_EQ(sts_sm_encode(100, STS_SIGN_BIT_POSITION), 0x0064);
    CHECK_EQ(sts_sm_encode(2048, STS_SIGN_BIT_POSITION), 0x0800);
    CHECK_EQ(sts_sm_encode(0, STS_SIGN_BIT_POSITION), 0x0000);

    /* 負載的方向位在 BIT10：−100 → 0x0400 | 100 = 0x0464 */
    CHECK_EQ(sts_sm_encode(-100, STS_SIGN_BIT_LOAD), 0x0464);

    /* Homing Offset 的方向位在 BIT11：−100 → 0x0800 | 100 = 0x0864 */
    CHECK_EQ(sts_sm_encode(-100, STS_SIGN_BIT_OFFSET), 0x0864);

    /* 超出範圍時截在最大值，不會溢進方向位 */
    CHECK_EQ(sts_sm_encode(40000, STS_SIGN_BIT_POSITION), 0x7FFF);
    CHECK_EQ(sts_sm_encode(-40000, STS_SIGN_BIT_POSITION), 0xFFFF);
    CHECK_EQ(sts_sm_encode(2000, STS_SIGN_BIT_LOAD), 0x03FF);   /* BIT10 以下最大 1023 */

    /* 極端值：INT32_MIN 取絕對值時不能溢位 */
    CHECK_EQ(sts_sm_encode(INT32_MIN, STS_SIGN_BIT_POSITION), 0xFFFF);
}

static void test_sm_decode(void)
{
    CHECK_EQ(sts_sm_decode(0x8064, STS_SIGN_BIT_POSITION), -100);
    CHECK_EQ(sts_sm_decode(0x0064, STS_SIGN_BIT_POSITION), 100);
    CHECK_EQ(sts_sm_decode(0x0464, STS_SIGN_BIT_LOAD), -100);
    CHECK_EQ(sts_sm_decode(0x0864, STS_SIGN_BIT_OFFSET), -100);

    /* 對照：若誤用 int16_t 轉型，0x8064 會讀成 −32668 */
    CHECK_EQ((int16_t)0x8064, -32668);

    /* 「負零」0x8000 解成 0 */
    CHECK_EQ(sts_sm_decode(0x8000, STS_SIGN_BIT_POSITION), 0);

    /* 負載只看 BIT10 以下與 BIT10；更高的位元忽略（0xFC64 的 BIT10 = 1，BIT11–15 是雜訊） */
    CHECK_EQ(sts_sm_decode(0xFC64, STS_SIGN_BIT_LOAD), -100);

    /* 編碼再解碼回到原值 */
    for (int32_t v = -4095; v <= 4095; v += 13)
        CHECK_EQ(sts_sm_decode(sts_sm_encode(v, STS_SIGN_BIT_POSITION), STS_SIGN_BIT_POSITION), v);
}

int main(void)
{
    test_little_endian();
    test_sm_encode();
    test_sm_decode();
    return CHECK_SUMMARY();
}
