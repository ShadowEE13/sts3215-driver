/*
 * sts_protocol.c — checksum 與組指令包
 */
#include "sts_protocol.h"

uint8_t sts_checksum(const uint8_t *bytes, size_t n)
{
    /*
     * 最多 253 個 byte、每個最大 255，總和 < 65536。用 uint32_t 累加，
     * 在 int 只有 16 bit 的 MCU 上也不會溢位。
     */
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += bytes[i];

    /*
     * 取反後只留最低一個 byte。~ 會把高位元也翻成 1，不 & 0xFF 的話
     * 高位會汙染結果（protocol-notes 2.3 的提醒）。
     */
    return (uint8_t)(~sum & 0xFFu);
}

size_t sts_build_packet(uint8_t *out, size_t cap, uint8_t id, uint8_t inst,
                        const uint8_t *params, size_t nparams)
{
    if (nparams > STS_MAX_PARAMS) return 0;
    size_t total = nparams + STS_PACKET_OVERHEAD;
    if (cap < total) return 0;

    out[0] = 0xFF;                          /* 字頭 */
    out[1] = 0xFF;
    out[2] = id;
    out[3] = (uint8_t)(nparams + 2);        /* Length = 參數個數 + 2，上面已確認 ≤ 250 */
    out[4] = inst;
    for (size_t i = 0; i < nparams; i++) out[5 + i] = params[i];

    /* checksum 涵蓋 out[2]（ID）到 out[total - 2]（最後一個參數），共 total - 3 個 byte */
    out[total - 1] = sts_checksum(&out[2], total - 3);
    return total;
}
