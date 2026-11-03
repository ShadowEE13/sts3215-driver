# sts3215-driver
#
#   make            編譯所有測試與 demo
#   make test       編譯並執行 tests/test_*.c，任一失敗就停
#   make demo       編譯並執行 sim/demo.c（模擬器自我驗證，不經過驅動）
#   make layers     分層檢查：src/ 不得引用 sim/ 或 OS 專屬 API
#   make SAN=1 ...  加上 AddressSanitizer + UBSan（抓越界、未定義行為）
#   make clean

CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -g -Iinclude -Isim

ifeq ($(SAN),1)
CFLAGS += -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
endif

BUILD := build

# 驅動本體（Protocol / Device / HAL 實作）
SRC := $(wildcard src/*.c)
# 模擬器（不含 demo.c，demo 自己有 main）
SIM := $(filter-out sim/demo.c,$(wildcard sim/*.c))
# 所有 header：任一變動就全部重編（專案小，不值得做精細的相依追蹤）
HDRS := $(wildcard include/*.h sim/*.h tests/*.h)

# 每個 tests/test_xxx.c 各自編成一支 build/test_xxx
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))
DEMO  := $(if $(wildcard sim/demo.c),$(BUILD)/demo)

.PHONY: all test demo layers clean

all: $(TESTS) $(DEMO)

$(BUILD):
	mkdir -p $@

# 測試 = 測試檔 + 驅動 + 模擬器
$(BUILD)/test_%: tests/test_%.c $(SRC) $(SIM) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# demo 只連模擬器，不連驅動：驗收 A 必須獨立於驅動
$(BUILD)/demo: sim/demo.c $(SIM) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "== all tests passed"

demo: $(DEMO)
	./$(DEMO)

# Protocol 層要能原封不動搬到 STM32，所以 src/ 裡（hal_pc.c 除外）不能出現：
#   模擬器的 header、termios / unistd 等 POSIX API、malloc
layers:
	@if grep -nE '#include *"(servo_sim|hal_sim)\.h"|termios|unistd\.h|malloc' \
	        $(filter-out src/hal_pc.c,$(SRC)) /dev/null; then \
	    echo "✖ 分層違規（見上方）"; exit 1; \
	else echo "✔ 分層檢查通過"; fi

clean:
	rm -rf $(BUILD)
