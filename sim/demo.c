/*
 * demo.c — 模擬器自我驗證（不經過驅動）
 *
 * 第一段是驗收 A：手冊範例封包（docs/protocol-notes.md 第 4 節）原封不動送進模擬器，
 * 回應與手冊逐 byte 比對。手冊是唯一的外部參照，這段不可以改成用程式組封包。
 *
 * 之後是模擬器本身的行為檢查（運動模型、SYNC、故障注入），封包由 demo 自己組——
 * checksum 用的是 demo 的第三份實作，不是模擬器的，也不是驅動的。
 *
 *   make demo
 */
#include <stdio.h>
#include <string.h>

#include "hal_sim.h"
#include "servo_sim.h"
#include "../tests/check.h"

static sim_bus_t bus;
static hal_sim_t hs;
static sts_hal_t hal;

/* 重建一條 bus，掛上 ID 1..n 的預設馬達 */
static void reset_bus(uint8_t n)
{
    sim_init(&bus);
    for (uint8_t id = 1; id <= n; id++) sim_add_servo(&bus, id);
    hal_sim_init(&hs, &bus);
    hal = (sts_hal_t){ &hal_sim_ops, &hs };
}

static void dump(const char *tag, const uint8_t *b, size_t n)
{
    printf("     %s", tag);
    if (n == 0) printf(" (none)");
    for (size_t i = 0; i < n; i++) printf(" %02X", b[i]);
    printf("\n");
}

/* checksum = ~(ID + Length + 其後到 checksum 前的所有 byte)；n 不含 checksum 本身 */
static uint8_t cks(const uint8_t *pkt, size_t n)
{
    unsigned sum = 0;
    for (size_t i = 2; i < n; i++) sum += pkt[i];
    return (uint8_t)(~sum & 0xFF);
}

/* 組 FF FF id len code params cks；指令包 code = instruction，應答包 code = error */
static size_t pkt(uint8_t *out, uint8_t id, uint8_t code, const uint8_t *p, size_t n)
{
    out[0] = 0xFF;
    out[1] = 0xFF;
    out[2] = id;
    out[3] = (uint8_t)(n + 2);
    out[4] = code;
    for (size_t i = 0; i < n; i++) out[5 + i] = p[i];
    out[5 + n] = cks(out, 5 + n);
    return n + 6;
}

/* flush → write → 讀到 want 個 byte、或 50 ms 沒有新資料為止 */
static size_t transact(const uint8_t *tx, size_t txlen, uint8_t *rx, size_t cap, size_t want)
{
    hal_flush_rx(&hal);
    hal_write(&hal, tx, txlen);
    dump("->", tx, txlen);

    size_t n = 0;
    uint32_t last = hal_millis(&hal);
    while (n < want && !hal_timed_out(last, hal_millis(&hal), 50)) {
        int r = hal_read(&hal, rx + n, cap - n);
        if (r > 0) {
            n += (size_t)r;
            last = hal_millis(&hal);
        }
    }
    dump("<-", rx, n);
    return n;
}

/* 回應必須與 expect 逐 byte 相同，且之後沒有多出來的 byte */
static void expect_reply(const uint8_t *tx, size_t txlen, const uint8_t *expect, size_t elen)
{
    uint8_t rx[HAL_SIM_RX_CAP];
    size_t n = transact(tx, txlen, rx, sizeof rx, elen);
    CHECK_EQ(n, elen);
    if (n == elen && elen > 0) CHECK_BYTES(rx, expect, elen);
    CHECK_EQ(hs.rx_len, 0);
}

/* WRITE 並確認回正常應答 */
static void write_ok(uint8_t id, uint8_t addr, const uint8_t *data, size_t n)
{
    uint8_t p[64], tx[80], ack[8];
    p[0] = addr;
    memcpy(p + 1, data, n);
    size_t txlen = pkt(tx, id, 0x03, p, n + 1);
    size_t alen  = pkt(ack, id, 0x00, NULL, 0);
    expect_reply(tx, txlen, ack, alen);
}

#define PEEK(id, addr) sim_peek(&bus, (id), (addr))
#define CHECK_CKS(p)   CHECK_EQ(cks((p), sizeof(p) - 1), (p)[sizeof(p) - 1])

/* ------------------------------------------------------------------------- */

static void acceptance_a(void)
{
    static const uint8_t ping[]      = {0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
    static const uint8_t ping_ack[]  = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};
    static const uint8_t rd[]        = {0xFF, 0xFF, 0x01, 0x04, 0x02, 0x38, 0x02, 0xBE};
    static const uint8_t rd_ack[]    = {0xFF, 0xFF, 0x01, 0x04, 0x00, 0x18, 0x05, 0xDD};
    static const uint8_t wr[]        = {0xFF, 0xFF, 0x01, 0x09, 0x03, 0x2A,
                                        0x00, 0x08, 0x00, 0x00, 0xE8, 0x03, 0xD5};
    static const uint8_t wr_ack[]    = {0xFF, 0xFF, 0x01, 0x02, 0x00, 0xFC};
    static const uint8_t reset_hdr[] = {0xFF, 0xFF, 0x01, 0x02, 0x0A};

    puts("== Acceptance A: manual examples -> simulator -> compare with manual");
    reset_bus(1);

    puts("  -- manual checksums themselves (computed by demo)");
    CHECK_CKS(ping);
    CHECK_CKS(ping_ack);
    CHECK_CKS(rd);
    CHECK_CKS(rd_ack);
    CHECK_CKS(wr);
    CHECK_CKS(wr_ack);
    CHECK_EQ(cks(reset_hdr, sizeof reset_hdr), 0xF2);   /* 手冊 1.3.9 印 F6，有誤（F-02） */

    puts("  -- 4.1 PING");
    expect_reply(ping, sizeof ping, ping_ack, sizeof ping_ack);

    puts("  -- 4.2 READ present position (precondition: sim_poke 0x38 = 1304)");
    sim_poke(&bus, 1, SIM_ADDR_PRESENT_POS, 1304);
    expect_reply(rd, sizeof rd, rd_ack, sizeof rd_ack);

    puts("  -- 4.3 WRITE goal 2048, time 0, speed 1000");
    expect_reply(wr, sizeof wr, wr_ack, sizeof wr_ack);
    CHECK_EQ(PEEK(1, SIM_ADDR_GOAL_POS), 2048);
    CHECK_EQ(PEEK(1, SIM_ADDR_GOAL_TIME), 0);
    CHECK_EQ(PEEK(1, SIM_ADDR_GOAL_SPEED), 1000);
}

/* 接續驗收 A 的狀態：當前 1304、目標 2048、速度 1000 步/s、扭矩關閉 */
static void motion(void)
{
    puts("== Motion model");

    puts("  -- torque off: no movement after 100 ms");
    hal_sim_advance(&hs, 100);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 1304);

    puts("  -- torque on, +300 ms: moved 300 steps");
    write_ok(1, SIM_ADDR_TORQUE, (const uint8_t[]){1}, 1);
    hal_sim_advance(&hs, 300);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 1604);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_SPD), 1000);
    CHECK_EQ(PEEK(1, SIM_ADDR_MOVING), 1);

    puts("  -- +500 ms: 444 steps left, arrives and stops");
    hal_sim_advance(&hs, 500);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 2048);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_SPD), 0);
    CHECK_EQ(PEEK(1, SIM_ADDR_MOVING), 0);

    puts("  -- reverse: present speed is sign-magnitude (BIT15)");
    write_ok(1, SIM_ADDR_GOAL_POS, (const uint8_t[]){0x9C, 0x07}, 2);   /* 1948 */
    hal_sim_advance(&hs, 50);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 1998);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_SPD), 0x8000 | 1000);
    hal_sim_advance(&hs, 100);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 1948);

    /* F-15：Lock 決定「EPROM 的值要不要存進 flash」，不是「能不能寫」 */
    puts("  -- Lock = 1: EPROM write (max limit 0x0B) takes effect, lost on power cycle");
    write_ok(1, SIM_ADDR_LOCK, (const uint8_t[]){1}, 1);
    write_ok(1, SIM_ADDR_MAX_LIMIT, (const uint8_t[]){0xB8, 0x0B}, 2);  /* 3000 */
    CHECK_EQ(PEEK(1, SIM_ADDR_MAX_LIMIT), 3000);
    sim_power_cycle(&bus, 1);
    CHECK_EQ(PEEK(1, SIM_ADDR_MAX_LIMIT), 4095);

    puts("  -- after power cycle: Lock 0, torque off, position kept, goal = position");
    CHECK_EQ(PEEK(1, SIM_ADDR_LOCK), 0);
    CHECK_EQ(PEEK(1, SIM_ADDR_TORQUE), 0);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 1948);
    CHECK_EQ(PEEK(1, SIM_ADDR_GOAL_POS), 1948);

    puts("  -- Lock = 0: EPROM write survives power cycle; 0x30 copied from 0x10 at power-on");
    write_ok(1, SIM_ADDR_MAX_LIMIT, (const uint8_t[]){0xB8, 0x0B}, 2);  /* 3000 */
    write_ok(1, SIM_ADDR_MAX_TORQUE, (const uint8_t[]){0x20, 0x03}, 2); /* 800 */
    CHECK_EQ(PEEK(1, SIM_ADDR_TORQUE_LIMIT), 1000);                     /* 還沒重開，0x30 不變 */
    sim_power_cycle(&bus, 1);
    CHECK_EQ(PEEK(1, SIM_ADDR_MAX_LIMIT), 3000);
    CHECK_EQ(PEEK(1, SIM_ADDR_MAX_TORQUE), 800);
    CHECK_EQ(PEEK(1, SIM_ADDR_TORQUE_LIMIT), 800);

    puts("  -- 0x32-0x36 undefined: write ignored; 0x31 (high byte of 0x30) writable (F-16)");
    write_ok(1, 0x32, (const uint8_t[]){0x55}, 1);
    CHECK_EQ(PEEK(1, 0x32), 0);
    write_ok(1, 0x36, (const uint8_t[]){0x55}, 1);
    CHECK_EQ(PEEK(1, 0x36), 0);
    write_ok(1, SIM_ADDR_TORQUE_LIMIT, (const uint8_t[]){0xF4, 0x01}, 2); /* 500 */
    CHECK_EQ(PEEK(1, SIM_ADDR_TORQUE_LIMIT), 500);

    puts("  -- goal 4000 beyond max limit 3000: stops at 3000, goal register kept");
    write_ok(1, SIM_ADDR_TORQUE, (const uint8_t[]){1}, 1);              /* power cycle 後扭矩是關的 */
    write_ok(1, SIM_ADDR_GOAL_POS,                                      /* 位置 4000、時間 0、速度 1000 */
             (const uint8_t[]){0xA0, 0x0F, 0x00, 0x00, 0xE8, 0x03}, 6);
    hal_sim_advance(&hs, 2000);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 3000);
    CHECK_EQ(PEEK(1, SIM_ADDR_GOAL_POS), 4000);

    puts("  -- write to read-only present position ignored");
    write_ok(1, SIM_ADDR_PRESENT_POS, (const uint8_t[]){0x00, 0x00}, 2);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 3000);

    puts("  -- READ reflects the motion model");
    uint8_t tx[16], exp[16];
    size_t txlen = pkt(tx, 1, 0x02, (const uint8_t[]){0x38, 0x02}, 2);
    size_t elen  = pkt(exp, 1, 0x00, (const uint8_t[]){0xB8, 0x0B}, 2);
    expect_reply(tx, txlen, exp, elen);

    /* F-17：Goal_Velocity = 0 是停止還是最高速，由 Phase（0x12）BIT3 決定 */
    puts("  -- speed 0, Phase BIT3 = 0 (default): does not move");
    write_ok(1, SIM_ADDR_GOAL_POS,                                      /* 位置 2000、時間 0、速度 0 */
             (const uint8_t[]){0xD0, 0x07, 0x00, 0x00, 0x00, 0x00}, 6);
    hal_sim_advance(&hs, 100);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 3000);
    CHECK_EQ(PEEK(1, SIM_ADDR_MOVING), 0);

    puts("  -- speed 0, Phase BIT3 = 1: moves at SIM_MAX_SPEED (3000 step/s)");
    write_ok(1, SIM_ADDR_PHASE, (const uint8_t[]){SIM_PHASE_SPEED0_MAX}, 1);
    hal_sim_advance(&hs, 100);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_POS), 2700);
    CHECK_EQ(PEEK(1, SIM_ADDR_PRESENT_SPD), 0x8000 | SIM_MAX_SPEED);
}

/* 上電預設值：預期值照抄 docs/memory-table-check.md 第四節（原廠記憶體表的初始值） */
static void defaults(void)
{
    puts("== Power-on defaults (vendor memory table)");
    reset_bus(1);
    CHECK_EQ(PEEK(1, SIM_ADDR_MODEL), 777);
    CHECK_EQ(PEEK(1, SIM_ADDR_MAX_TEMP), 70);
    CHECK_EQ(PEEK(1, SIM_ADDR_UNLOAD_COND), 44);
    CHECK_EQ(PEEK(1, SIM_ADDR_LED_ALARM), 47);
    CHECK_EQ(PEEK(1, SIM_ADDR_CW_DEADZONE), 1);
    CHECK_EQ(PEEK(1, SIM_ADDR_CCW_DEADZONE), 1);
    CHECK_EQ(PEEK(1, SIM_ADDR_RESOLUTION), 1);
    CHECK_EQ(PEEK(1, SIM_ADDR_PROT_TORQUE), 20);
    CHECK_EQ(PEEK(1, SIM_ADDR_PROT_TIME), 200);
    CHECK_EQ(PEEK(1, SIM_ADDR_OVLD_TORQUE), 80);
    CHECK_EQ(PEEK(1, SIM_ADDR_OC_TIME), 200);
    CHECK_EQ(PEEK(1, SIM_ADDR_LOCK), 0);
    CHECK_EQ(PEEK(1, SIM_ADDR_PHASE), 0);      /* 原廠表沒給，模擬器的選擇（V-14） */
    CHECK_EQ(PEEK(1, SIM_ADDR_TORQUE), 0);
}

static void sync_ops(void)
{
    uint8_t tx[64], exp[128];
    size_t txlen, elen;

    puts("== SYNC READ / SYNC WRITE / broadcast (6 servos)");
    reset_bus(6);
    for (uint8_t id = 1; id <= 6; id++)
        sim_poke(&bus, id, SIM_ADDR_PRESENT_POS, (uint16_t)(1000 + 100 * id));

    /* TODO: 手冊附的 SYNC READ / SYNC WRITE 範例尚未收進 protocol-notes，補上後在這裡逐 byte 比對 */

    puts("  -- SYNC READ 0x38 x 2 bytes, IDs 1-6: 6 packets in ID order, 48 bytes");
    txlen = pkt(tx, 0xFE, 0x82, (const uint8_t[]){0x38, 0x02, 1, 2, 3, 4, 5, 6}, 8);
    elen = 0;
    for (uint8_t id = 1; id <= 6; id++) {
        uint16_t pos = (uint16_t)(1000 + 100 * id);
        elen += pkt(exp + elen, id, 0x00, (const uint8_t[]){(uint8_t)pos, (uint8_t)(pos >> 8)}, 2);
    }
    CHECK_EQ(elen, 48);
    expect_reply(tx, txlen, exp, elen);

    puts("  -- SYNC WRITE goal position IDs 1-6: no reply, verified by register");
    uint8_t p[2 + 6 * 3] = {0x2A, 0x02};
    for (uint8_t id = 1; id <= 6; id++) {
        uint16_t goal = (uint16_t)(2000 + 10 * id);
        p[2 + (id - 1) * 3]     = id;
        p[2 + (id - 1) * 3 + 1] = (uint8_t)goal;
        p[2 + (id - 1) * 3 + 2] = (uint8_t)(goal >> 8);
    }
    txlen = pkt(tx, 0xFE, 0x83, p, sizeof p);
    expect_reply(tx, txlen, NULL, 0);
    for (uint8_t id = 1; id <= 6; id++)
        CHECK_EQ(PEEK(id, SIM_ADDR_GOAL_POS), 2000 + 10 * id);

    puts("  -- broadcast PING: every servo replies in ID order (real bus would collide)");
    txlen = pkt(tx, 0xFE, 0x01, NULL, 0);
    elen = 0;
    for (uint8_t id = 1; id <= 6; id++) elen += pkt(exp + elen, id, 0x00, NULL, 0);
    expect_reply(tx, txlen, exp, elen);

    puts("  -- broadcast WRITE: all execute, no reply");
    txlen = pkt(tx, 0xFE, 0x03, (const uint8_t[]){SIM_ADDR_TORQUE, 1}, 2);
    expect_reply(tx, txlen, NULL, 0);
    for (uint8_t id = 1; id <= 6; id++) CHECK_EQ(PEEK(id, SIM_ADDR_TORQUE), 1);

    puts("  -- broadcast READ: no reply");
    txlen = pkt(tx, 0xFE, 0x02, (const uint8_t[]){0x38, 0x02}, 2);
    expect_reply(tx, txlen, NULL, 0);
}

static void faults(void)
{
    uint8_t ping1[8], ping2[8], ack1[8], ack2[8], tx[32], exp[128], buf[32];
    size_t txlen, elen;
    int r;

    puts("== Fault injection");
    reset_bus(6);
    pkt(ping1, 1, 0x01, NULL, 0);
    pkt(ping2, 2, 0x01, NULL, 0);
    pkt(ack1, 1, 0x00, NULL, 0);
    pkt(ack2, 2, 0x00, NULL, 0);

    puts("  -- SYNC READ with ID 3 muted: 5 packets");
    sim_find(&bus, 3)->fault.mute = true;
    txlen = pkt(tx, 0xFE, 0x82, (const uint8_t[]){0x38, 0x02, 1, 2, 3, 4, 5, 6}, 8);
    elen = 0;
    for (uint8_t id = 1; id <= 6; id++)
        if (id != 3) elen += pkt(exp + elen, id, 0x00, (const uint8_t[]){0x00, 0x08}, 2);
    expect_reply(tx, txlen, exp, elen);
    sim_find(&bus, 3)->fault.mute = false;

    puts("  -- drop_next = 1: first PING lost, second answered");
    sim_find(&bus, 1)->fault.drop_next = 1;
    expect_reply(ping1, 6, NULL, 0);
    expect_reply(ping1, 6, ack1, 6);

    puts("  -- force_error = 0x04 (overheat): shows in error byte, checksum still valid");
    sim_find(&bus, 1)->fault.force_error = 0x04;
    elen = pkt(exp, 1, 0x04, NULL, 0);
    expect_reply(ping1, 6, exp, elen);
    sim_find(&bus, 1)->fault.force_error = 0;

    puts("  -- corrupt_next = 1: checksum off by one bit, next reply clean");
    sim_find(&bus, 1)->fault.corrupt_next = 1;
    memcpy(exp, ack1, 6);
    exp[5] ^= 0x01;
    expect_reply(ping1, 6, exp, 6);
    expect_reply(ping1, 6, ack1, 6);

    puts("  -- noise: 00 FF 12 before, FF after; one-shot");
    sim_inject_noise(&bus, (const uint8_t[]){0x00, 0xFF, 0x12}, 3, (const uint8_t[]){0xFF}, 1);
    memcpy(exp, (const uint8_t[]){0x00, 0xFF, 0x12}, 3);
    memcpy(exp + 3, ack1, 6);
    exp[9] = 0xFF;
    expect_reply(ping1, 6, exp, 10);
    expect_reply(ping1, 6, ack1, 6);

    puts("  -- bad checksum in instruction: silently dropped");
    memcpy(tx, ping1, 6);
    tx[5] ^= 0x01;
    expect_reply(tx, 6, NULL, 0);

    puts("  -- instruction split across two writes: reassembled");
    hal_write(&hal, ping1, 3);
    CHECK_EQ(hs.rx_len, 0);
    hal_write(&hal, ping1 + 3, 3);
    r = hal_read(&hal, buf, sizeof buf);
    CHECK_EQ(r, 6);
    CHECK_BYTES(buf, ack1, 6);

    puts("  -- rx_chunk = 1: one byte per read");
    hs.rx_chunk = 1;
    hal_write(&hal, ping1, 6);
    CHECK_EQ(hal_read(&hal, buf, sizeof buf), 1);
    CHECK_EQ(hal_read(&hal, buf, sizeof buf), 1);
    hs.rx_chunk = 0;
    CHECK_EQ(hal_read(&hal, buf, sizeof buf), 4);

    puts("  -- latency_ms = 5: reply readable exactly 5 ms after write");
    hs.latency_ms = 5;
    hal_write(&hal, ping1, 6);
    uint32_t t0 = hal_millis(&hal);
    while ((r = hal_read(&hal, buf, sizeof buf)) == 0 && !hal_timed_out(t0, hal_millis(&hal), 100)) {}
    CHECK_EQ(r, 6);
    CHECK_EQ(hal_millis(&hal) - t0, 5);

    puts("  -- late reply survives flush: arrives ahead of the next transaction's reply");
    hs.latency_ms = 20;
    hal_flush_rx(&hal);
    hal_write(&hal, ping1, 6);           /* 20 ms 後才到 */
    hal_sim_advance(&hs, 10);               /* 驅動等 10 ms 就放棄 */
    hs.latency_ms = 0;
    hal_flush_rx(&hal);                  /* 舊回應還在路上，清不掉 */
    elen = transact(ping2, 6, exp, sizeof exp, 12);
    CHECK_EQ(elen, 12);
    CHECK_BYTES(exp, ack1, 6);              /* 先到的是 ID 1 的舊回應 */
    CHECK_BYTES(exp + 6, ack2, 6);
}

int main(void)
{
    acceptance_a();
    motion();
    defaults();
    sync_ops();
    faults();
    return CHECK_SUMMARY();
}
