/*
 * sts_protocol.h — Protocol 層：封包格式、checksum、組指令包
 *
 * 指令包（Controller → Servo），格式見 docs/protocol-notes.md 第 2 節：
 *
 *     FF FF | ID | Length | Instruction | Param1 … ParamN | Checksum
 *
 *     Length   = N + 2（參數個數 + Instruction 與 Checksum 各 1）
 *     Checksum = ~(ID + Length + Instruction + Param1 + … + ParamN) & 0xFF
 *
 * 這一層只處理 bytes，不知道暫存器的意義（那是 Device 層），也不碰硬體（那是 HAL）。
 * 全部是純函式：沒有全域變數，buffer 由呼叫者提供。
 */
#ifndef STS_PROTOCOL_H
#define STS_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STS_BROADCAST_ID     0xFE

/* 指令碼（docs/protocol-notes.md 第 3 節） */
#define STS_INST_PING        0x01
#define STS_INST_READ        0x02
#define STS_INST_WRITE       0x03
#define STS_INST_REG_WRITE   0x04
#define STS_INST_ACTION      0x05
#define STS_INST_RECOVERY    0x06
#define STS_INST_RESET       0x0A
#define STS_INST_SYNC_READ   0x82
#define STS_INST_SYNC_WRITE  0x83

/*
 * 長度上限。Length 欄位只有 1 byte，但官方 SDK 收包時只接受 Length ≤ 250
 * （LeRobot-notes 第 5 節的合理性檢查），步驟 6 的 parser 也會照這個檢查。
 * 送出去的包用同一個上限，確保「自己組的包自己的 parser 一定收得下」。
 */
#define STS_MAX_LENGTH       250
#define STS_MAX_PARAMS       (STS_MAX_LENGTH - 2)    /* 248 */
#define STS_PACKET_OVERHEAD  6                       /* FF FF ID Length Inst/Error Checksum */
#define STS_MAX_PACKET       (STS_MAX_PARAMS + STS_PACKET_OVERHEAD)   /* 254 */

/*
 * 計算 checksum。bytes 指向 ID，n = 從 ID 到最後一個參數的 byte 數
 * （不含開頭的 FF FF，也不含 checksum 本身）。
 * 指令包與應答包的算法相同，步驟 6 的 parser 驗證回應時也用這個。
 */
uint8_t sts_checksum(const uint8_t *bytes, size_t n);

/*
 * 組一個指令包寫進 out，回傳整包長度（= nparams + 6）。
 * nparams 超過 STS_MAX_PARAMS，或 out 的大小 cap 放不下時回傳 0，out 內容不保證。
 * nparams 為 0 時 params 可以是 NULL。
 */
size_t sts_build_packet(uint8_t *out, size_t cap, uint8_t id, uint8_t inst,
                        const uint8_t *params, size_t nparams);

#ifdef __cplusplus
}
#endif

#endif /* STS_PROTOCOL_H */
