import socket, struct, time

MAGIC, VERSION = 0x5343, 0x01
CMD_JOINT, CMD_NOP = 0x01, 0x00
ADDR = ("127.0.0.1", 49200)

def now_us() -> int:
    # 必須是 Unix epoch 起算的牆鐘微秒，見 §2.4。
    # 不要換成 perf_counter / monotonic：epoch 不同，跨行程相減無意義。
    # Windows 上需 Python >= 3.13，否則解析度只有約 15.6 ms；
    # 卡在舊版時改用 §2.4 的 ctypes 版本。
    return time.time_ns() // 1000

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def build(cmd_type: int, seq: int, payload: bytes = b"") -> bytes:
    header = struct.pack(
        "<HBBIQB3s",
        MAGIC, VERSION, cmd_type, seq,
        now_us(), #timestamp
        len(payload), b"\x00\x00\x00",
    )
    body = header + payload
    return body + struct.pack("<H", crc16_ccitt(body))

def joint_payload(degrees) -> bytes:
    return struct.pack("<6i", *(int(d * 1000) for d in degrees))

##### Main #####
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) 
seq = 0

##### Loop #####
while True:
    seq += 1
    sock.sendto(build(CMD_JOINT, seq, joint_payload([0, 0, 0, 0, 0, 0])), ADDR)
    time.sleep(0.01)