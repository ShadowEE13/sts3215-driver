/*
 * servo_sim.h — 虛擬 STS3215：Control Table + 協定處理 + 運動模型 + 故障注入
 *
 * 一個 sim_bus_t 代表一條 bus，上面最多掛 SIM_MAX_SERVOS 顆馬達。
 * 指令包從 sim_receive() 餵進來，回應包從同一個呼叫拿回去；時間由呼叫者用
 * sim_tick() 推進。通常不直接用，而是透過 hal_sim.h 接到 hal_ops_t。
 *
 * 刻意不和驅動共用任何程式碼（含 checksum、sign-magnitude）：
 * 兩邊各寫一次，才能互相驗證協定理解。
 *
 * 所有對外名稱都加 sim_ / SIM_ 前綴，測試會同時 include 驅動與模擬器的 header。
 */
#ifndef SERVO_SIM_H
#define SERVO_SIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIM_MAX_SERVOS   8
#define SIM_TABLE_SIZE   128
#define SIM_BROADCAST_ID 0xFE
#define SIM_MAX_SPEED    3000   /* 步/s；Goal_Velocity 超過、或為 0 且 Phase BIT3 = 1 時用這個（待真機驗證） */
#define SIM_EPROM_END    0x28   /* 0x00–0x27 是 EPROM 區，0x28 起是 SRAM 區 */
#define SIM_IN_CAP       512    /* 收包緩衝，須 ≥ 最長封包 4 + 255 */
#define SIM_NOISE_MAX    32

/*
 * 模擬器有用到的 Control Table 位址
 * （完整表見 docs/protocol-notes.md 第 5 節；與原廠表的核對見 docs/memory-table-check.md）
 */
enum {
    SIM_ADDR_MODEL        = 0x03,   /* 2 bytes，唯讀，777 */
    SIM_ADDR_ID           = 0x05,
    SIM_ADDR_BAUD         = 0x06,
    SIM_ADDR_RETURN_DELAY = 0x07,
    SIM_ADDR_STATUS_LEVEL = 0x08,   /* 0 = 只回 PING / READ，1 = 全部都回 */
    SIM_ADDR_MIN_LIMIT    = 0x09,   /* 2 bytes */
    SIM_ADDR_MAX_LIMIT    = 0x0B,   /* 2 bytes */
    SIM_ADDR_MAX_TEMP     = 0x0D,
    SIM_ADDR_MAX_TORQUE   = 0x10,   /* 2 bytes，上電時複製到 0x30 */
    SIM_ADDR_PHASE        = 0x12,   /* bitmask，見 SIM_PHASE_* */
    SIM_ADDR_UNLOAD_COND  = 0x13,   /* 卸載條件 bitmask */
    SIM_ADDR_LED_ALARM    = 0x14,   /* LED 報警條件 bitmask */
    SIM_ADDR_CW_DEADZONE  = 0x1A,
    SIM_ADDR_CCW_DEADZONE = 0x1B,
    SIM_ADDR_RESOLUTION   = 0x1E,
    SIM_ADDR_OFFSET       = 0x1F,   /* 2 bytes，Homing Offset，sign BIT11（不模擬） */
    SIM_ADDR_PROT_TORQUE  = 0x22,
    SIM_ADDR_PROT_TIME    = 0x23,
    SIM_ADDR_OVLD_TORQUE  = 0x24,
    SIM_ADDR_OC_TIME      = 0x26,
    SIM_ADDR_TORQUE       = 0x28,
    SIM_ADDR_ACCEL        = 0x29,
    SIM_ADDR_GOAL_POS     = 0x2A,   /* 2 bytes，sign BIT15 */
    SIM_ADDR_GOAL_TIME    = 0x2C,   /* 2 bytes */
    SIM_ADDR_GOAL_SPEED   = 0x2E,   /* 2 bytes，sign BIT15 */
    SIM_ADDR_TORQUE_LIMIT = 0x30,   /* 2 bytes */
    SIM_ADDR_LOCK         = 0x37,   /* 0 = 寫 EPROM 掉電保存；1 = 寫入立即生效、掉電不保存（F-15） */
    SIM_ADDR_PRESENT_POS  = 0x38,   /* 2 bytes，唯讀，sign BIT15 */
    SIM_ADDR_PRESENT_SPD  = 0x3A,   /* 2 bytes，唯讀，sign BIT15 */
    SIM_ADDR_PRESENT_LOAD = 0x3C,   /* 2 bytes，唯讀，sign BIT10 */
    SIM_ADDR_VOLTAGE      = 0x3E,   /* 唯讀，0.1V */
    SIM_ADDR_TEMPERATURE  = 0x3F,   /* 唯讀，°C */
    SIM_ADDR_STATUS       = 0x41,   /* 唯讀，error byte 的來源 */
    SIM_ADDR_MOVING       = 0x42,   /* 唯讀 */
    SIM_ADDR_CURRENT      = 0x45,   /* 2 bytes，唯讀 */
};

/* Phase（0x12）的 bit：1 = Goal_Velocity 為 0 時以最高速移動；0 = 速度 0 就停止（F-17） */
#define SIM_PHASE_SPEED0_MAX 0x08

/*
 * 故障注入，直接改欄位即可，例：sim_find(&bus, 3)->fault.drop_next = 1;
 * 「回應」指這顆馬達本來會送出的一個 Status Packet（SYNC READ 時每顆各算一個）。
 */
typedef struct {
    bool    mute;          /* 永遠不回應：斷線、ID 不存在 */
    uint8_t drop_next;     /* 接下來 N 個回應不送，之後恢復：測重傳後成功 */
    uint8_t force_error;   /* OR 進每個回應的 error byte：測安全層分支 */
    uint8_t corrupt_next;  /* 接下來 N 個回應的 checksum 翻一個 bit：測 parser 拒收 */
} sim_fault_t;

typedef struct {
    bool        present;                 /* 這個欄位有沒有掛馬達 */
    uint8_t     table[SIM_TABLE_SIZE];   /* Control Table，ID 也存在這裡（0x05） */
    uint8_t     eprom[SIM_EPROM_END];    /* 存在 flash 裡的 EPROM 區：斷電重開時還原成這份 */
    uint32_t    frac;                    /* 運動模型：未滿一步的累積量，單位 1/1000 步 */
    sim_fault_t fault;
} sim_servo_t;

typedef struct {
    sim_servo_t servo[SIM_MAX_SERVOS];

    /* 收包緩衝：指令包可以分好幾次 sim_receive 送進來 */
    uint8_t in[SIM_IN_CAP];
    size_t  in_len;

    /* 下一次處理到指令包時，回應前後各插入一次的雜訊，用完自動清掉 */
    uint8_t noise_pre[SIM_NOISE_MAX];
    size_t  noise_pre_len;
    uint8_t noise_post[SIM_NOISE_MAX];
    size_t  noise_post_len;
} sim_bus_t;

/* 清空整條 bus（沒有任何馬達、沒有雜訊） */
void sim_init(sim_bus_t *bus);

/* 掛一顆預設值的馬達上去；滿了回 NULL。預設值見 sim/README.md */
sim_servo_t *sim_add_servo(sim_bus_t *bus, uint8_t id);

/* 依目前的 ID（0x05）找馬達；找不到回 NULL */
sim_servo_t *sim_find(sim_bus_t *bus, uint8_t id);

/*
 * 測試鉤子：模擬這顆馬達斷電再上電。
 *   - EPROM 區還原成 flash 裡保存的值（Lock = 1 時寫入的會消失）
 *   - SRAM 區回到上電預設值：扭矩關、Lock = 0、0x30 由 0x10 複製
 *   - 當前位置保留（編碼器量的是實際角度），目標位置設成當前位置
 *   - 故障注入設定不受影響（那是測試環境，不是馬達的狀態）
 * 找不到 id 時不做事。
 */
void sim_power_cycle(sim_bus_t *bus, uint8_t id);

/*
 * 指令方向：把 in[0..len) 送上 bus，回應寫進 out，回傳寫了幾 byte。
 * 不完整的封包留在內部緩衝等下一次；checksum 錯的封包靜靜丟掉（同真機）。
 * out 不夠放時截斷。
 */
size_t sim_receive(sim_bus_t *bus, const uint8_t *in, size_t len,
                   uint8_t *out, size_t cap);

/* 運動模型前進 dt_ms 毫秒 */
void sim_tick(sim_bus_t *bus, uint32_t dt_ms);

/*
 * 測試鉤子：不經過協定、不看讀寫屬性，直接讀寫暫存器。
 * 2-byte 暫存器（見上方 enum）一次讀寫兩個 byte，little-endian；其餘 1 byte。
 * poke 到 EPROM 區時連 flash 映像一起改，當成「出廠就是這個值」，power cycle 後仍保留。
 * 找不到 id 時 poke 不做事、peek 回 0。
 */
void     sim_poke(sim_bus_t *bus, uint8_t id, uint8_t addr, uint16_t value);
uint16_t sim_peek(sim_bus_t *bus, uint8_t id, uint8_t addr);

/* 設定下一次回應前後的雜訊（各最多 SIM_NOISE_MAX bytes，超過截斷） */
void sim_inject_noise(sim_bus_t *bus, const uint8_t *pre, size_t pre_len,
                      const uint8_t *post, size_t post_len);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_SIM_H */
