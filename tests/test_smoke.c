/*
 * test_smoke.c — 確認工具鏈、編譯旗標、測試框架本身正常
 *
 * 等 src/ 有東西之後可以刪掉，或留著當最小範例。
 */
#include <stdint.h>
#include "check.h"

int main(void)
{
    /* 範例 4.1 PING 封包，手冊原文 */
    const uint8_t ping[] = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};

    CHECK(sizeof ping == 6);
    CHECK_EQ(ping[3], 2);             /* Length = 參數數 0 + 2 */
    CHECK_BYTES(ping, ping, sizeof ping);

    /*
     * -Wconversion 示範：checksum 通常用 int 累加，最後再塞回 uint8_t。
     * 若最後一行寫成
     *     uint8_t cks = ~sum;
     * gcc 和 clang 都會編譯失敗：int → uint8_t 會截斷，編譯器要你明確寫出
     * 「我知道會截斷」。截斷正是 checksum 要的行為，所以明確 & 0xFF 再轉型。
     *
     * 注意 gcc 和 clang 抓的範圍不同：
     *     uint16_t v = lo | (hi << 8);   // clang 報錯，gcc 不報
     * 所以偶爾用 make CC=clang 編一次（macOS 的 cc 本來就是 clang）。
     */
    int sum = 0;
    for (int i = 2; i < 5; i++) sum += ping[i];      /* ID + Length + Inst */
    uint8_t cks = (uint8_t)(~sum & 0xFF);
    CHECK_EQ(cks, 0xFB);

    return CHECK_SUMMARY();
}
