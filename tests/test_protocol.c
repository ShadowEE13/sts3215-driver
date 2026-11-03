/*
 * test_protocol.c — 步驟 5：checksum 與組指令包；驗收 B（驅動組包 vs 手冊）
 *
 * 預期值一律逐 byte 照抄 docs/protocol-notes.md 第 4 節，寫成常數陣列，
 * 不用受測的程式碼產生。驅動若誤解協定，這裡會跟手冊對不上。
 */
#include <stdint.h>

#include "check.h"
#include "sts_device.h"
#include "sts_protocol.h"

/* ---- 手冊原文（protocol-notes 第 4 節） ---- */

/* 4.1 PING ID 1 */
static const uint8_t ping_tx[]  = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
static const uint8_t ping_rx[]  = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};

/* 4.2 READ ID 1，位址 0x38，長度 2 */
static const uint8_t read_tx[]  = {0xFF, 0xFF, 0x01, 0x04, 0x02, 0x38, 0x02, 0xBE};
static const uint8_t read_rx[]  = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05, 0xDD};

/* 4.3 WRITE ID 1，從 0x2A 起寫 位置 2048、時間 0、速度 1000 */
static const uint8_t write_tx[] = {0xFF, 0xFF, 0x01, 0x09, 0x03, 0x2A,
                                   0x00, 0x08, 0x00, 0x00, 0xE8, 0x03, 0xD5};
static const uint8_t write_rx[] = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};

/* RESET ID 1：手冊 1.3.9 印 F6 是錯的，依公式應為 F2（F-02，已驗證） */
static const uint8_t reset_tx[] = {0xFF, 0xFF, 0x01, 0x02, 0x0A, 0xF2};

/* ------------------------------------------------------------------------- */

/* 封包最後一個 byte 是 checksum，涵蓋 pkt[2] 到倒數第二個 byte */
static uint8_t checksum_of(const uint8_t *pkt, size_t len)
{
    return sts_checksum(&pkt[2], len - 3);
}

static void test_checksum_against_manual(void)
{
    /* 手冊上每一包（指令包與應答包）的 checksum 都要算得出來 */
    CHECK_EQ(checksum_of(ping_tx, sizeof ping_tx), 0xFB);
    CHECK_EQ(checksum_of(ping_rx, sizeof ping_rx), 0xFC);
    CHECK_EQ(checksum_of(read_tx, sizeof read_tx), 0xBE);
    CHECK_EQ(checksum_of(read_rx, sizeof read_rx), 0xDD);
    CHECK_EQ(checksum_of(write_tx, sizeof write_tx), 0xD5);
    CHECK_EQ(checksum_of(write_rx, sizeof write_rx), 0xFC);

    /* 總和超過 255 時只取最低 byte：0xFF + 0x02 = 0x101 → 取 0x01 → 取反 0xFE */
    static const uint8_t over[] = {0xFF, 0x02};
    CHECK_EQ(sts_checksum(over, sizeof over), 0xFE);
}

/* 驗收 B：驅動組出來的指令包與手冊 byte-for-byte 相同 */
static void test_acceptance_b(void)
{
    uint8_t out[STS_MAX_PACKET];
    size_t n;

    /* 4.1 PING：沒有參數 */
    n = sts_build_packet(out, sizeof out, 0x01, STS_INST_PING, NULL, 0);
    CHECK_EQ(n, sizeof ping_tx);
    CHECK_BYTES(out, ping_tx, sizeof ping_tx);

    /* 4.2 READ：參數 = 首地址、長度 */
    static const uint8_t read_params[] = {0x38, 0x02};
    n = sts_build_packet(out, sizeof out, 0x01, STS_INST_READ, read_params, sizeof read_params);
    CHECK_EQ(n, sizeof read_tx);
    CHECK_BYTES(out, read_tx, sizeof read_tx);

    /*
     * 4.3 WRITE：參數 = 首地址 + 三個 16-bit 值。
     * 參數用 sts_le16_put 組，順便驗證 little-endian 的順序與手冊一致。
     */
    uint8_t write_params[7];
    write_params[0] = 0x2A;
    sts_le16_put(&write_params[1], 2048);   /* 目標位置 */
    sts_le16_put(&write_params[3], 0);      /* 運行時間：位置模式填 0 */
    sts_le16_put(&write_params[5], 1000);   /* 運行速度 */
    n = sts_build_packet(out, sizeof out, 0x01, STS_INST_WRITE, write_params, sizeof write_params);
    CHECK_EQ(n, sizeof write_tx);
    CHECK_BYTES(out, write_tx, sizeof write_tx);

    /* RESET：checksum 依公式計算，不是照抄手冊的 F6 */
    n = sts_build_packet(out, sizeof out, 0x01, STS_INST_RESET, NULL, 0);
    CHECK_EQ(n, sizeof reset_tx);
    CHECK_BYTES(out, reset_tx, sizeof reset_tx);
}

static void test_build_limits(void)
{
    uint8_t out[STS_MAX_PACKET + 1];
    static const uint8_t params[STS_MAX_PARAMS + 1] = {0};

    /* buffer 剛好夠 → 成功；少 1 byte → 回 0 */
    CHECK_EQ(sts_build_packet(out, 6, 0x01, STS_INST_PING, NULL, 0), 6);
    CHECK_EQ(sts_build_packet(out, 5, 0x01, STS_INST_PING, NULL, 0), 0);

    /* 參數個數上限：248 個 → Length 250、整包 254 bytes；249 個 → 回 0 */
    CHECK_EQ(sts_build_packet(out, sizeof out, 0x01, STS_INST_WRITE, params, STS_MAX_PARAMS),
             STS_MAX_PACKET);
    CHECK_EQ(out[3], STS_MAX_LENGTH);
    CHECK_EQ(sts_build_packet(out, sizeof out, 0x01, STS_INST_WRITE, params, STS_MAX_PARAMS + 1), 0);
}

int main(void)
{
    test_checksum_against_manual();
    test_acceptance_b();
    test_build_limits();
    return CHECK_SUMMARY();
}
