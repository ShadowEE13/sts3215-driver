/*
 * test_hal.c — 步驟 1：HAL 介面本身（include/sts_hal.h）
 *
 * sts_hal.h 只有型別和 inline 函式，這裡驗兩件事：
 *   1. hal_timed_out() 在計數器溢位前後都正確（決定 3）
 *   2. 函式表與實例分開的用法（決定 4、5、6）：兩條 bus 共用一張 static const
 *      函式表，hal_write / hal_read 等包裝把各自的 ctx 正確轉交，彼此互不干擾
 *
 * 第 2 項用一個極簡的假 HAL（loopback：write 進去的 bytes 原樣變成可讀的資料），
 * 不用模擬器——這一步只驗介面，不驗任何真正的實作。
 */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "sts_hal.h"

/* ---- 假 HAL：一條 bus 的所有狀態都放在 ctx 指向的 struct 裡 ---- */

typedef struct {
    uint8_t  buf[32];   /* 已寫入、還沒讀走的 bytes */
    size_t   len;
    uint32_t now;       /* 假時鐘，測試直接設定 */
    int      flushes;   /* flush_rx 被呼叫幾次 */
} loop_port_t;

static int loop_write(void *ctx, const uint8_t *buf, size_t len)
{
    loop_port_t *p = ctx;
    if (len > sizeof p->buf - p->len) return -1;    /* 放不下：以負值回報錯誤 */
    memcpy(p->buf + p->len, buf, len);
    p->len += len;
    return (int)len;                                 /* 實際送出的 byte 數 */
}

static int loop_read(void *ctx, uint8_t *buf, size_t cap)
{
    loop_port_t *p = ctx;
    size_t n = p->len < cap ? p->len : cap;         /* 有多少回多少，最多 cap */
    memcpy(buf, p->buf, n);
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
    return (int)n;
}

static void loop_flush_rx(void *ctx)
{
    loop_port_t *p = ctx;
    p->len = 0;
    p->flushes++;
}

static uint32_t loop_millis(void *ctx)
{
    const loop_port_t *p = ctx;
    return p->now;
}

/* 函式表只有一張，而且是 const（決定 5） */
static const hal_ops_t loop_ops = {
    .write    = loop_write,
    .read     = loop_read,
    .flush_rx = loop_flush_rx,
    .millis   = loop_millis,
};

/* ------------------------------------------------------------------------- */

static void test_timed_out(void)
{
    /* 一般情況：經過時間「達到」timeout 才算逾時 */
    CHECK(!hal_timed_out(100, 109, 10));
    CHECK(hal_timed_out(100, 110, 10));
    CHECK(hal_timed_out(100, 500, 10));

    /* 溢位：start = 0xFFFFFFFB，now 繞回 4 → 實際經過 9 ms；繞回 5 → 經過 10 ms */
    CHECK(!hal_timed_out(0xFFFFFFFBu, 4u, 10));
    CHECK(hal_timed_out(0xFFFFFFFBu, 5u, 10));

    /*
     * 對照組：直覺寫法 now >= start + timeout 在溢位時會出錯。
     * start + 10 繞回 5，所以才過 1 ms（now = 0xFFFFFFFC）就被判定逾時。
     */
    uint32_t start = 0xFFFFFFFBu, now = 0xFFFFFFFCu;
    CHECK(now >= start + 10u);               /* 錯誤寫法：說已經逾時 */
    CHECK(!hal_timed_out(start, now, 10));   /* 正確寫法：只過了 1 ms */

    /* timeout = 0：立刻逾時 */
    CHECK(hal_timed_out(123, 123, 0));
}

static void test_two_buses(void)
{
    static const uint8_t ping[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};   /* 手冊 4.1 */
    uint8_t buf[16];

    loop_port_t a = {0}, b = {0};
    sts_hal_t leader   = { &loop_ops, &a };
    sts_hal_t follower = { &loop_ops, &b };

    /* 兩條 bus 共用同一張函式表，只有 ctx 不同（決定 5） */
    CHECK(leader.ops == follower.ops);
    CHECK(leader.ctx != follower.ctx);

    /* write 回傳實際送出的 byte 數（決定 6） */
    CHECK_EQ(hal_write(&leader, ping, sizeof ping), 6);

    /* 資料只進到 leader 的 ctx，follower 不受影響（決定 4） */
    CHECK_EQ(a.len, 6);
    CHECK_EQ(b.len, 0);

    /* 非阻塞：沒有資料就回 0，不會卡住（決定 1） */
    CHECK_EQ(hal_read(&follower, buf, sizeof buf), 0);

    /* read 最多回 cap 個，剩下的留到下一次 */
    CHECK_EQ(hal_read(&leader, buf, 4), 4);
    CHECK_BYTES(buf, ping, 4);
    CHECK_EQ(hal_read(&leader, buf, sizeof buf), 2);
    CHECK_BYTES(buf, ping + 4, 2);
    CHECK_EQ(hal_read(&leader, buf, sizeof buf), 0);

    /* flush_rx 只清自己那條 bus */
    hal_write(&leader, ping, sizeof ping);
    hal_write(&follower, ping, sizeof ping);
    hal_flush_rx(&leader);
    CHECK_EQ(a.len, 0);
    CHECK_EQ(a.flushes, 1);
    CHECK_EQ(b.len, 6);
    CHECK_EQ(b.flushes, 0);

    /* millis 也是各自的時鐘 */
    a.now = 1000;
    b.now = 2000;
    CHECK_EQ(hal_millis(&leader), 1000);
    CHECK_EQ(hal_millis(&follower), 2000);

    /* 錯誤以負值回報（決定 6） */
    uint8_t big[64] = {0};
    CHECK(hal_write(&follower, big, sizeof big) < 0);
}

int main(void)
{
    test_timed_out();
    test_two_buses();
    return CHECK_SUMMARY();
}
