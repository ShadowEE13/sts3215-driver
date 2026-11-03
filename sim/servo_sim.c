/*
 * servo_sim.c — 虛擬 STS3215
 *
 * parser 刻意寫得直白：找 FF FF → 等收滿 → 驗 checksum → 執行。
 * 不追求效率；跟手冊不確定的行為一律列進 sim/README.md 的「待真機驗證」。
 */
#include "servo_sim.h"

#include <string.h>

/* 指令碼（只在這個檔案裡用） */
enum {
    INST_PING       = 0x01,
    INST_READ       = 0x02,
    INST_WRITE      = 0x03,
    INST_SYNC_READ  = 0x82,
    INST_SYNC_WRITE = 0x83,
};

/* ---------------------------------------------------------------------------
 * Control Table
 * ------------------------------------------------------------------------- */

typedef enum { ATTR_RO, ATTR_EPROM, ATTR_SRAM } attr_t;

/*
 * 讀寫屬性依位址區段決定（protocol-notes 第 5 節）：
 *   0x00–0x04  版本 / 型號          唯讀
 *   0x05–0x27  EPROM               Lock(0x37) 為 0 才能寫
 *   0x28–0x37  SRAM 控制區          可寫
 *   0x38–      回授區與未公開區      唯讀
 */
static attr_t attr_of(size_t addr)
{
    if (addr <= 0x04) return ATTR_RO;
    if (addr <= 0x27) return ATTR_EPROM;
    if (addr <= 0x37) return ATTR_SRAM;
    return ATTR_RO;
}

/* sim_poke / sim_peek 用：哪些位址是 2-byte 暫存器 */
static size_t reg_width(uint8_t addr)
{
    switch (addr) {
    case SIM_ADDR_MODEL:
    case SIM_ADDR_MIN_LIMIT:
    case SIM_ADDR_MAX_LIMIT:
    case SIM_ADDR_MAX_TORQUE:
    case SIM_ADDR_OFFSET:
    case SIM_ADDR_GOAL_POS:
    case SIM_ADDR_GOAL_TIME:
    case SIM_ADDR_GOAL_SPEED:
    case SIM_ADDR_TORQUE_LIMIT:
    case SIM_ADDR_PRESENT_POS:
    case SIM_ADDR_PRESENT_SPD:
    case SIM_ADDR_PRESENT_LOAD:
    case SIM_ADDR_CURRENT:
        return 2;
    default:
        return 1;
    }
}

static uint16_t get16(const sim_servo_t *s, uint8_t addr)
{
    return (uint16_t)(s->table[addr] | (s->table[addr + 1] << 8));
}

static void put16(sim_servo_t *s, uint8_t addr, uint16_t v)
{
    s->table[addr]     = (uint8_t)(v & 0xFF);
    s->table[addr + 1] = (uint8_t)(v >> 8);
}

/* sign-magnitude（不是二補數）：sign_bit 為方向位，以下為絕對值 */
static int32_t sm_to_int(uint16_t raw, unsigned sign_bit)
{
    unsigned u = raw;    /* 先轉 unsigned：uint16_t 運算時會被提升成 int，跟 1u 混用會觸發 -Wsign-conversion */
    int32_t mag = (int32_t)(u & ((1u << sign_bit) - 1u));
    return ((u >> sign_bit) & 1u) ? -mag : mag;
}

static uint16_t int_to_sm(int32_t v, unsigned sign_bit)
{
    uint32_t mag = (uint32_t)(v < 0 ? -v : v) & ((1u << sign_bit) - 1u);
    return (uint16_t)(v < 0 ? (mag | (1u << sign_bit)) : mag);
}

/* 依屬性逐 byte 寫入；不能寫的 byte 靜靜跳過（應答照常，見 README） */
static void write_table(sim_servo_t *s, size_t addr, const uint8_t *data, size_t n)
{
    for (size_t i = 0; i < n && addr + i < SIM_TABLE_SIZE; i++) {
        attr_t at = attr_of(addr + i);
        if (at == ATTR_RO) continue;
        if (at == ATTR_EPROM && s->table[SIM_ADDR_LOCK] != 0) continue;
        s->table[addr + i] = data[i];
    }
}

/* ---------------------------------------------------------------------------
 * 回應
 * ------------------------------------------------------------------------- */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    bool     started;   /* 這次 sim_receive 是否已執行過指令包（雜訊只插一次） */
} out_t;

static void out_put(out_t *o, uint8_t b)
{
    if (o->len < o->cap) o->buf[o->len++] = b;
}

/* 送出一個 Status Packet：FF FF ID LEN ERR params CKS，並套用故障注入 */
static void respond(sim_servo_t *s, const uint8_t *params, size_t n, out_t *o)
{
    if (s->fault.mute) return;
    if (s->fault.drop_next > 0) {
        s->fault.drop_next--;
        return;
    }

    uint8_t id  = s->table[SIM_ADDR_ID];
    uint8_t len = (uint8_t)(n + 2);
    uint8_t err = (uint8_t)(s->table[SIM_ADDR_STATUS] | s->fault.force_error);
    unsigned sum = (unsigned)id + len + err;

    out_put(o, 0xFF);
    out_put(o, 0xFF);
    out_put(o, id);
    out_put(o, len);
    out_put(o, err);
    for (size_t i = 0; i < n; i++) {
        out_put(o, params[i]);
        sum += params[i];
    }

    uint8_t cks = (uint8_t)(~sum & 0xFF);
    if (s->fault.corrupt_next > 0) {
        s->fault.corrupt_next--;
        cks = (uint8_t)(cks ^ 0x01);
    }
    out_put(o, cks);
}

/* ---------------------------------------------------------------------------
 * 指令執行
 * ------------------------------------------------------------------------- */

/* PING / READ / WRITE 對單顆馬達的效果 */
static void execute_one(sim_servo_t *s, bool bcast, uint8_t inst,
                        const uint8_t *p, size_t n, out_t *o)
{
    switch (inst) {
    case INST_PING:                                 /* 廣播 PING 也會回 */
        respond(s, NULL, 0, o);
        break;

    case INST_READ:                                 /* params: addr len */
        if (bcast || n != 2 || p[0] + p[1] > SIM_TABLE_SIZE) break;
        respond(s, &s->table[p[0]], p[1], o);
        break;

    case INST_WRITE:                                /* params: addr data… */
        if (n < 2) break;
        write_table(s, p[0], p + 1, n - 1);
        if (!bcast && s->table[SIM_ADDR_STATUS_LEVEL] != 0)
            respond(s, NULL, 0, o);                 /* 若改了 ID，這裡已是新 ID */
        break;

    default:                                        /* REG WRITE、ACTION、RESET…：未實作 */
        break;
    }
}

static void execute_sync(sim_bus_t *bus, uint8_t inst, const uint8_t *p, size_t n, out_t *o)
{
    if (n < 2) return;
    uint8_t addr = p[0], len = p[1];

    if (inst == INST_SYNC_READ) {
        /* params: addr len id1 id2 …；依封包裡的 ID 順序逐顆回應，不存在的 ID 跳過 */
        if (addr + len > SIM_TABLE_SIZE) return;
        for (size_t i = 2; i < n; i++) {
            sim_servo_t *s = sim_find(bus, p[i]);
            if (s) respond(s, &s->table[addr], len, o);
        }
    } else {
        /* params: addr len (id data×len)×k；廣播，無回應 */
        size_t block = (size_t)len + 1;
        if (len == 0 || (n - 2) % block != 0) return;
        for (size_t i = 2; i < n; i += block) {
            sim_servo_t *s = sim_find(bus, p[i]);
            if (s) write_table(s, addr, p + i + 1, len);
        }
    }
}

static void execute(sim_bus_t *bus, uint8_t id, uint8_t inst,
                    const uint8_t *p, size_t n, out_t *o)
{
    if (!o->started) {
        o->started = true;
        for (size_t i = 0; i < bus->noise_pre_len; i++) out_put(o, bus->noise_pre[i]);
    }

    bool bcast = (id == SIM_BROADCAST_ID);

    if (inst == INST_SYNC_READ || inst == INST_SYNC_WRITE) {
        if (bcast) execute_sync(bus, inst, p, n, o);
        return;
    }

    if (!bcast) {
        sim_servo_t *s = sim_find(bus, id);
        if (s) execute_one(s, false, inst, p, n, o);
        return;
    }

    for (size_t k = 0; k < SIM_MAX_SERVOS; k++)
        if (bus->servo[k].present) execute_one(&bus->servo[k], true, inst, p, n, o);
}

/* ---------------------------------------------------------------------------
 * 收包
 * ------------------------------------------------------------------------- */

static void drop(sim_bus_t *bus, size_t n)
{
    memmove(bus->in, bus->in + n, bus->in_len - n);
    bus->in_len -= n;
}

/* 執行緩衝區裡所有完整的指令包；開頭的垃圾丟掉，不完整的留著等下一次 */
static void parse(sim_bus_t *bus, out_t *o)
{
    for (;;) {
        if (bus->in_len < 2) return;
        if (bus->in[0] != 0xFF || bus->in[1] != 0xFF) { drop(bus, 1); continue; }
        if (bus->in_len < 4) return;
        if (bus->in[2] == 0xFF) { drop(bus, 1); continue; }     /* FF FF FF：多一個 FF */

        uint8_t length = bus->in[3];
        if (length < 2) { drop(bus, 1); continue; }
        size_t total = (size_t)length + 4;
        if (bus->in_len < total) return;

        unsigned sum = 0;
        for (size_t i = 2; i < total - 1; i++) sum += bus->in[i];
        if ((uint8_t)(~sum & 0xFF) != bus->in[total - 1]) { drop(bus, 1); continue; }

        execute(bus, bus->in[2], bus->in[4], bus->in + 5, (size_t)length - 2, o);
        drop(bus, total);
    }
}

size_t sim_receive(sim_bus_t *bus, const uint8_t *in, size_t len,
                   uint8_t *out, size_t cap)
{
    out_t o = { out, cap, 0, false };

    for (size_t i = 0; i < len; i++) {
        if (bus->in_len == SIM_IN_CAP) drop(bus, 1);   /* 只有垃圾會塞滿 */
        bus->in[bus->in_len++] = in[i];
        parse(bus, &o);
    }

    if (o.started) {
        for (size_t i = 0; i < bus->noise_post_len; i++) out_put(&o, bus->noise_post[i]);
        bus->noise_pre_len = 0;
        bus->noise_post_len = 0;
    }
    return o.len;
}

/* ---------------------------------------------------------------------------
 * 運動模型
 * ------------------------------------------------------------------------- */

static void halt(sim_servo_t *s)
{
    s->frac = 0;
    put16(s, SIM_ADDR_PRESENT_SPD, 0);
    s->table[SIM_ADDR_MOVING] = 0;
}

/* 等速朝目標前進：不模擬加速度、負載、扭矩限制 */
static void step(sim_servo_t *s, uint32_t dt_ms)
{
    if (s->table[SIM_ADDR_TORQUE] != 1) {
        halt(s);
        return;
    }

    int32_t pos  = sm_to_int(get16(s, SIM_ADDR_PRESENT_POS), 15);
    int32_t goal = sm_to_int(get16(s, SIM_ADDR_GOAL_POS), 15);
    int32_t lo   = (int32_t)get16(s, SIM_ADDR_MIN_LIMIT);
    int32_t hi   = (int32_t)get16(s, SIM_ADDR_MAX_LIMIT);
    if (goal < lo) goal = lo;
    if (goal > hi) goal = hi;

    if (pos == goal) {
        halt(s);
        return;
    }

    uint32_t speed = get16(s, SIM_ADDR_GOAL_SPEED) & 0x7FFFu;   /* 位置模式只看大小 */
    if (speed == 0 || speed > SIM_MAX_SPEED) speed = SIM_MAX_SPEED;

    uint64_t travel = s->frac + (uint64_t)speed * dt_ms;        /* 單位 1/1000 步 */
    uint32_t dist   = (uint32_t)(goal > pos ? goal - pos : pos - goal);

    if (travel / 1000 >= dist) {
        put16(s, SIM_ADDR_PRESENT_POS, int_to_sm(goal, 15));
        halt(s);
        return;
    }

    int32_t dir = goal > pos ? 1 : -1;
    s->frac = (uint32_t)(travel % 1000);
    put16(s, SIM_ADDR_PRESENT_POS, int_to_sm(pos + dir * (int32_t)(travel / 1000), 15));
    put16(s, SIM_ADDR_PRESENT_SPD, int_to_sm(dir * (int32_t)speed, 15));
    s->table[SIM_ADDR_MOVING] = 1;
}

void sim_tick(sim_bus_t *bus, uint32_t dt_ms)
{
    for (size_t k = 0; k < SIM_MAX_SERVOS; k++)
        if (bus->servo[k].present) step(&bus->servo[k], dt_ms);
}

/* ---------------------------------------------------------------------------
 * 建立 / 查詢 / 測試鉤子
 * ------------------------------------------------------------------------- */

void sim_init(sim_bus_t *bus)
{
    memset(bus, 0, sizeof *bus);
}

sim_servo_t *sim_add_servo(sim_bus_t *bus, uint8_t id)
{
    for (size_t k = 0; k < SIM_MAX_SERVOS; k++) {
        sim_servo_t *s = &bus->servo[k];
        if (s->present) continue;

        memset(s, 0, sizeof *s);
        s->present = true;
        put16(s, SIM_ADDR_MODEL, 777);
        s->table[SIM_ADDR_ID]           = id;
        s->table[SIM_ADDR_BAUD]         = 0;      /* 1 Mbps */
        s->table[SIM_ADDR_RETURN_DELAY] = 250;    /* 500 µs；模擬器不看，延遲用 hal_sim 的 latency_ms */
        s->table[SIM_ADDR_STATUS_LEVEL] = 1;
        put16(s, SIM_ADDR_MIN_LIMIT, 0);
        put16(s, SIM_ADDR_MAX_LIMIT, 4095);
        put16(s, SIM_ADDR_MAX_TORQUE, 1000);
        put16(s, SIM_ADDR_TORQUE_LIMIT, 1000);
        s->table[SIM_ADDR_LOCK]         = 1;
        put16(s, SIM_ADDR_PRESENT_POS, 2048);
        put16(s, SIM_ADDR_GOAL_POS, 2048);        /* 與當前位置相同：開扭矩不會突然跳動 */
        s->table[SIM_ADDR_VOLTAGE]      = 120;
        s->table[SIM_ADDR_TEMPERATURE]  = 25;
        return s;
    }
    return NULL;
}

sim_servo_t *sim_find(sim_bus_t *bus, uint8_t id)
{
    for (size_t k = 0; k < SIM_MAX_SERVOS; k++) {
        sim_servo_t *s = &bus->servo[k];
        if (s->present && s->table[SIM_ADDR_ID] == id) return s;
    }
    return NULL;
}

void sim_poke(sim_bus_t *bus, uint8_t id, uint8_t addr, uint16_t value)
{
    sim_servo_t *s = sim_find(bus, id);
    if (!s || addr + reg_width(addr) > SIM_TABLE_SIZE) return;

    if (reg_width(addr) == 2) put16(s, addr, value);
    else                      s->table[addr] = (uint8_t)value;

    if (addr == SIM_ADDR_PRESENT_POS) s->frac = 0;
}

uint16_t sim_peek(sim_bus_t *bus, uint8_t id, uint8_t addr)
{
    sim_servo_t *s = sim_find(bus, id);
    if (!s || addr + reg_width(addr) > SIM_TABLE_SIZE) return 0;
    return reg_width(addr) == 2 ? get16(s, addr) : s->table[addr];
}

void sim_inject_noise(sim_bus_t *bus, const uint8_t *pre, size_t pre_len,
                      const uint8_t *post, size_t post_len)
{
    if (pre_len > SIM_NOISE_MAX)  pre_len = SIM_NOISE_MAX;
    if (post_len > SIM_NOISE_MAX) post_len = SIM_NOISE_MAX;
    if (pre_len)  memcpy(bus->noise_pre, pre, pre_len);
    if (post_len) memcpy(bus->noise_post, post, post_len);
    bus->noise_pre_len  = pre_len;
    bus->noise_post_len = post_len;
}
