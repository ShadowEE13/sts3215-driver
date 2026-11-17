import socket

ADDR = ("127.0.0.1", 49200)

try:
    ##### Main #####
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(ADDR) #分配地址
        sock.settimeout(1) #設定逾時

        ##### Loop #####
        while True:
            try:
                data, addr = sock.recvfrom(512)
            except TimeoutError:
                continue
            print(f"{len(data)} bytes from {addr}: {data.hex(' ')}")

except KeyboardInterrupt:
    print("已停止")