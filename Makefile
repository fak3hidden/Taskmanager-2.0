# Task Manager 2.0 - tiny cross-platform task manager
#
#   make            build for the host (Linux / macOS / Windows-mingw)
#   make all-cross  build every target with zig cc  (ZIG="python3 -m ziglang" or ZIG=zig)
#   make test       run unit + integration tests
#   make screenshot render docs/*.bmp headless

CC      ?= cc
ZIG     ?= zig
CFLAGS  ?= -Os -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation -Wno-missing-field-initializers -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections
LDFLAGS ?= -s -Wl,--gc-sections
OUT     ?= build

COMMON  = src/main.c src/ui.c src/gfx.c src/font.c
UNAME   := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(OS),Windows_NT)
  HOST_SRC = $(COMMON) src/sys_win.c src/win_w32.c
  HOST_BIN = $(OUT)/taskmgr.exe
  HOST_LD  = -mwindows -lgdi32 -luser32 -ladvapi32 -liphlpapi -lpowrprof -s
else ifeq ($(UNAME),Darwin)
  HOST_SRC = $(COMMON) src/sys_mac.c src/win_mac.c
  HOST_BIN = $(OUT)/taskmgr
  HOST_LD  = -Wl,-dead_strip
else
  HOST_SRC = $(COMMON) src/sys_linux.c src/win_x11.c
  HOST_BIN = $(OUT)/taskmgr
  HOST_LD  = $(LDFLAGS) -ldl -lm
endif

.PHONY: all clean test all-cross screenshot size

all: $(HOST_BIN)

$(HOST_BIN): $(HOST_SRC) src/tm.h
	@mkdir -p $(OUT)
	$(CC) $(CFLAGS) -o $@ $(HOST_SRC) $(HOST_LD)

# ---------------------------------------------------------------- cross builds
ZCF = -Os -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-missing-field-initializers -Wno-format-truncation -Wno-missing-field-initializers -fno-unwind-tables -fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections

$(OUT)/linux-x86_64/taskmgr: $(COMMON) src/sys_linux.c src/win_x11.c src/tm.h
	@mkdir -p $(dir $@)
	$(ZIG) cc -target x86_64-linux-gnu.2.17 $(ZCF) -o $@ $(COMMON) src/sys_linux.c src/win_x11.c -ldl -lm -s -Wl,--gc-sections

$(OUT)/linux-aarch64/taskmgr: $(COMMON) src/sys_linux.c src/win_x11.c src/tm.h
	@mkdir -p $(dir $@)
	$(ZIG) cc -target aarch64-linux-gnu.2.17 $(ZCF) -o $@ $(COMMON) src/sys_linux.c src/win_x11.c -ldl -lm -s -Wl,--gc-sections

$(OUT)/windows-x86_64/taskmgr.exe: $(COMMON) src/sys_win.c src/win_w32.c src/tm.h
	@mkdir -p $(dir $@)
	$(ZIG) cc -target x86_64-windows-gnu $(ZCF) -o $@ $(COMMON) src/sys_win.c src/win_w32.c -Wl,--subsystem,windows -lgdi32 -luser32 -ladvapi32 -liphlpapi -lpowrprof -s -Wl,--gc-sections

$(OUT)/macos-x86_64/taskmgr: $(COMMON) src/sys_mac.c src/win_mac.c src/tm.h
	@mkdir -p $(dir $@)
	$(ZIG) cc -target x86_64-macos $(ZCF) -o $@ $(COMMON) src/sys_mac.c src/win_mac.c -Wl,-dead_strip

$(OUT)/macos-aarch64/taskmgr: $(COMMON) src/sys_mac.c src/win_mac.c src/tm.h
	@mkdir -p $(dir $@)
	$(ZIG) cc -target aarch64-macos $(ZCF) -o $@ $(COMMON) src/sys_mac.c src/win_mac.c -Wl,-dead_strip

all-cross: $(OUT)/linux-x86_64/taskmgr $(OUT)/linux-aarch64/taskmgr $(OUT)/windows-x86_64/taskmgr.exe \
           $(OUT)/macos-x86_64/taskmgr $(OUT)/macos-aarch64/taskmgr size

size:
	@echo; echo "binary sizes:"; ls -la $(OUT)/*/taskmgr* 2>/dev/null | awk '{printf "  %8d bytes  %s\n", $$5, $$9}'

# ---------------------------------------------------------------- tests
$(OUT)/test_unit: tests/test_unit.c src/gfx.c src/font.c src/ui.c src/sys_linux.c src/win_x11.c src/tm.h
	@mkdir -p $(OUT)
	$(CC) -g -O0 -std=c11 -Wall -Wno-unused-parameter -Wno-misleading-indentation -Wno-format-truncation -Wno-missing-field-initializers -Isrc -o $@ tests/test_unit.c src/gfx.c src/font.c src/ui.c src/sys_linux.c src/win_x11.c -ldl -lm

test: $(HOST_BIN) $(OUT)/test_unit
	$(OUT)/test_unit
	sh tests/test_cli.sh $(HOST_BIN)

screenshot: $(HOST_BIN)
	@mkdir -p docs
	$(HOST_BIN) --screenshot docs/processes.bmp --tab 0
	$(HOST_BIN) --screenshot docs/performance.bmp --tab 1
	$(HOST_BIN) --screenshot docs/details.bmp --tab 2

clean:
	rm -rf $(OUT)
