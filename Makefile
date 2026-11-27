# sts3215-driver
#
#   make            編譯所有測試與 demo
#   make test       編譯並執行 tests/test_*.c 與 sim/demo.c（驗收 A），任一失敗就停
#   make demo       編譯並執行 sim/demo.c（模擬器自我驗證，不經過驅動）
#   make layers     分層檢查：src/ 不得引用 sim/ 或 OS 專屬 API
#   make SAN=1 ...  加上 AddressSanitizer + UBSan（抓越界、未定義行為）
#   make strict     加嚴檢查：額外警告 + 以 C++ 編譯器 include 每個 header（review 用）
#   make clean

CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -Wconversion -g -Iinclude -Isim

ifeq ($(SAN),1)
CFLAGS += -O1 -fno-omit-frame-pointer -fsanitize=address,undefined
endif

BUILD := build

# Windows（MSYS2 / MinGW）的 gcc 會自動在輸出檔名補上 .exe。
# 目標名稱要跟著加，否則 make 找不到自己的產出，每次都重新編譯。
EXE := $(if $(filter Windows_NT,$(OS)),.exe)

# 驅動本體（Protocol / Device / HAL 實作）
SRC := $(wildcard src/*.c)
# 模擬器（不含 demo.c，demo 自己有 main）
SIM := $(filter-out sim/demo.c,$(wildcard sim/*.c))
# 所有 header：任一變動就全部重編（專案小，不值得做精細的相依追蹤）
HDRS := $(wildcard include/*.h sim/*.h tests/*.h)

# 每個 tests/test_xxx.c 各自編成一支 build/test_xxx
TESTS := $(patsubst tests/%.c,$(BUILD)/%$(EXE),$(wildcard tests/test_*.c))
DEMO  := $(if $(wildcard sim/demo.c),$(BUILD)/demo$(EXE))

.PHONY: all test demo layers strict clean

all: $(TESTS) $(DEMO)

$(BUILD):
	mkdir -p $@

# 測試 = 測試檔 + 驅動 + 模擬器
$(BUILD)/test_%$(EXE): tests/test_%.c $(SRC) $(SIM) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# demo 只連模擬器，不連驅動：驗收 A 必須獨立於驅動
$(BUILD)/demo$(EXE): sim/demo.c $(SIM) $(HDRS) | $(BUILD)
	$(CC) $(CFLAGS) $(filter %.c,$^) -o $@

# demo（驗收 A）也一起跑，改到模擬器時才不會漏掉；輸出很長，平常只印最後的統計，
# 失敗時才印全部（完整輸出用 make demo 看）
test: $(TESTS) $(DEMO)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@if [ -n "$(DEMO)" ]; then \
	    echo "== $(DEMO)（驗收 A + 模擬器自我檢查）"; \
	    ./$(DEMO) > $(BUILD)/demo.log || { cat $(BUILD)/demo.log; exit 1; }; \
	    tail -n 1 $(BUILD)/demo.log; \
	fi
	@echo "== all tests passed"

demo: $(DEMO)
	./$(DEMO)

# Protocol 層要能原封不動搬到 STM32，所以 src/ 裡（PC 端 HAL 除外）不能出現：
#   模擬器的 header、termios / unistd / windows.h 等平台 API、malloc
#
# 豁免清單抽成變數，並在檢查前確認它指向的檔案真的存在（C-44）。
# 2026/11 的教訓：豁免檔名曾寫死為 hal_pc.c，檔案改名成 hal_win32.c 之後
# filter-out 篩不掉任何東西，而 grep 樣式當時又漏了 windows.h，於是檢查照常
# 執行、照常印出「✔ 分層檢查通過」，實際上什麼都沒擋到。失敗是靜默的。
#
# 對策不是「改名後記得回頭 grep 舊檔名」——那仍然靠紀律，而這條教訓本身就是
# 「靠紀律的防護會靜默失效」。改成讓檢查證明自己的前提還成立：
#   1. 豁免清單指向不存在的檔案就當場失敗（改名當下就爆，不會拖三個月）
#   2. 通過訊息印出豁免了什麼，「✔ 通過」變成「✔ 通過（豁免：…）」，
#      盲目通過因此變成看得見
EXEMPT := src/hal_win32.c

layers:
	@for f in $(EXEMPT); do \
	    test -f $$f || { echo "✖ 豁免清單指向不存在的檔案：$$f（改名後忘了更新？）"; exit 1; }; \
	done
	@if grep -nE '#include *"(servo_sim|hal_sim)\.h"|termios|unistd\.h|windows\.h|malloc' \
	        $(filter-out $(EXEMPT),$(SRC)) /dev/null; then \
	    echo "✖ 分層違規（見上方）"; exit 1; \
	else echo "✔ 分層檢查通過（豁免：$(EXEMPT)）"; fi

# 加嚴檢查（W9 review 時手動跑的那幾項，寫成 target 以便重跑）
#   1. 額外警告：有號/無號轉換、變數遮蔽、非標準語法。平常的 make test 不開，
#      因為 -Wsign-conversion 在測試碼裡噪音多；review 或交接前跑一次
#   2. 以 C++ 編譯器逐一 include 每個 header：確認 extern "C" 包裝與
#      header 能單獨被 include（下學期要給隊友的 C++ / Unity 端用）
STRICT_FLAGS := -Wsign-conversion -Wshadow -Wpedantic
strict:
	$(CC) $(CFLAGS) $(STRICT_FLAGS) -fsyntax-only $(SRC) $(SIM) $(wildcard sim/demo.c) $(wildcard tests/*.c)
	@for h in $(wildcard include/*.h sim/*.h); do \
	    echo "== C++ include $$h"; \
	    echo "#include \"$$(basename $$h)\"" | $(CXX) -x c++ -Iinclude -Isim -fsyntax-only - || exit 1; \
	done
	@echo "✔ strict 通過"

clean:
	rm -rf $(BUILD)