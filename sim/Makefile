# SPDX-License-Identifier: Apache-2.0
# 小纸 Pico 桌面模拟器：main/ 原样编译，硬件驱动层换成 sim/src 里的替身。
# Read Pico desktop simulator: main/ is compiled as-is; hardware drivers are
# replaced by the stand-ins in sim/src.
#
#   make -C sim        构建 / build
#   make -C sim run    构建并运行 / build and run

ROOT := ..
.DEFAULT_GOAL := all
BUILD := build
BIN := $(BUILD)/read_pico_sim

CC ?= cc
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

INCLUDES := \
  -Iidf -Isrc \
  -I$(ROOT)/main -I$(ROOT)/main/app -I$(ROOT)/main/apps -I$(ROOT)/main/font \
  -I$(ROOT)/main/ui -I$(ROOT)/main/factory -I$(ROOT)/main/book -I$(ROOT)/main/dictionary \
  -I$(ROOT)/managed_components/espressif__cjson/cJSON \
  -I$(ROOT)/components/epdiy/src -I$(ROOT)/components/epdiy/include \
  -I$(ROOT)/components/e0470_epaper_waveform/include \
  -I$(ROOT)/components/e0470_epaper_waveform/waveforms \
  -I$(ROOT)/components/continuous_du/include \
  -I$(ROOT)/components/cst836u/include \
  -I$(ROOT)/components/sc7a20h/include \
  -I$(ROOT)/components/fca9555/include \
  -I$(ROOT)/components/sy7636a/include \
  -I$(ROOT)/components/read_pico/include \
  -I$(ROOT)/components/read_pico_pmu/include \
  -I$(ROOT)/components/read_pico_search/include \
  -I$(ROOT)/components/read_pico_transfer/include

CFLAGS ?= -O1 -g
CFLAGS += -std=gnu17 -D_DARWIN_C_SOURCE -DSIM_HOST=1 -Wall -Wno-unused-function \
  -Wno-unused-variable -Wno-format -Wno-deprecated-non-prototype -Wno-strict-prototypes \
  $(INCLUDES)

# 临时加编译参数用，例如 make EXTRA_CFLAGS='-Wextra -Werror' 做一次严格检查。
# For one-off flags, e.g. make EXTRA_CFLAGS='-Wextra -Werror' for a strict pass.
CFLAGS += $(EXTRA_CFLAGS) -DREADER_SIM_VERSION=\"$(shell cat $(ROOT)/version.txt)\"
$(BUILD)/sim/sim_idf.o: $(ROOT)/version.txt

# 固件源文件：强制包含 sim_fs.h 做路径重定向。
# Firmware sources: sim_fs.h is force-included for path redirection.
FW_SRCS := \
  $(wildcard $(ROOT)/main/*.c) \
  $(wildcard $(ROOT)/main/app/*.c) \
  $(ROOT)/main/board/reader_controls.c \
  $(ROOT)/main/board/reader_screenshot.c $(ROOT)/main/board/reader_wallpaper.c $(ROOT)/main/board/reader_clock.c \
  $(ROOT)/main/apps/app_reader.c $(ROOT)/main/apps/app_wifi_transfer.c $(ROOT)/main/apps/app_usb_transfer.c \
  $(wildcard $(ROOT)/main/ui/*.c) \
  $(wildcard $(ROOT)/main/font/*.c) \
  $(wildcard $(ROOT)/main/factory/*.c) \
  $(wildcard $(ROOT)/main/book/*.c) \
  $(wildcard $(ROOT)/main/dictionary/*.c) \
  $(wildcard $(ROOT)/main/annotations/*.c) \
  $(ROOT)/managed_components/espressif__cjson/cJSON/cJSON.c \
  $(ROOT)/main/book/vendor/tjpgd.c \
  $(ROOT)/components/epdiy/src/epdiy.c \
  $(ROOT)/components/epdiy/src/highlevel.c \
  $(ROOT)/components/epdiy/src/displays.c \
  $(ROOT)/components/e0470_epaper_waveform/e0470_epaper_waveform.c \
  $(ROOT)/components/e0470_epaper_waveform/e0470_waveform_trim.c \
  $(ROOT)/components/read_pico_search/read_pico_search.c \
  $(ROOT)/components/read_pico/read_pico_init.c \
  $(ROOT)/components/read_pico/read_pico_epd_timing.c \
  $(ROOT)/components/continuous_du/continuous_du.c \
  $(ROOT)/components/sc7a20h/sc7a20h.c \
  $(ROOT)/components/sy7636a/sy7636a.c

SIM_SRCS := $(wildcard src/*.c)

FW_OBJS := $(patsubst $(ROOT)/%.c,$(BUILD)/fw/%.o,$(FW_SRCS))
SIM_OBJS := $(patsubst src/%.c,$(BUILD)/sim/%.o,$(SIM_SRCS))

# macOS 上多一个菜单栏菜单（Objective-C）；其他平台没有菜单栏，sim_main.c 里对应的调用被宏关掉。
# macOS adds a menu-bar menu (Objective-C); elsewhere there is no menu bar and sim_main.c compiles the call out.
ifeq ($(shell uname),Darwin)
SIM_OBJS += $(BUILD)/sim/sim_menu.o
CFLAGS += -DSIM_HAS_MENU=1
SDL_LIBS += -framework Cocoa
endif
ASSET_OBJ := $(BUILD)/sim_assets.o

all: $(BIN) compile_flags.txt

$(BIN): $(FW_OBJS) $(SIM_OBJS) $(ASSET_OBJ)
	$(CC) -o $@ $^ $(SDL_LIBS) -lz -lm -lpthread

$(BUILD)/fw/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -include idf/sim_fs.h -MMD -MP -c $< -o $@

$(BUILD)/sim/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/sim/%.o: src/%.m
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=gnu17,$(CFLAGS)) $(SDL_CFLAGS) -fobjc-arc -MMD -MP -c $< -o $@

# 外壳渲染器逐像素计算，单独开高优化。/ The shell renderer works per pixel; optimize it harder.
$(BUILD)/sim/sim_shell.o: CFLAGS += -O3 -ffast-math

# 固件里用 EMBED_FILES 嵌入的资源，这里用汇编 .incbin 嵌入同名符号。
# Assets embedded via EMBED_FILES on the device; .incbin provides the same symbols here.
$(ASSET_OBJ): src/sim_assets.S $(wildcard $(ROOT)/main/assets/*) $(wildcard assets/*)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ -DASSET_DIR=$(abspath $(ROOT)/main/assets) -DSIM_ASSET_DIR=$(abspath assets)

# 模拟的机身存储：真机上是随固件烧进去的 builtin/storage 镜像，这里每次运行前照它补齐
# （只补缺的，不覆盖你在 data/flash 里改过的东西）。模拟 TF 卡是 data/sdcard，外加 ready/books 里整理好的书和仓库 sdcard/fonts 里的字体。
# Simulated internal storage: on the device it is the builtin/storage image flashed with the firmware;
# here it is topped up before each run (missing files only, never overwriting changes under data/flash).
# The simulated TF card holds whatever is under data/sdcard plus links to ready/books and the repo's sdcard/fonts.
.PHONY: seed
seed:
	@mkdir -p data/sdcard/books data/sdcard/fonts data/flash/books data/flash/fonts data/nvs
	@# 模拟卡上的书来自 ready/books（tools/prepare_books.py 整理好的书），字体来自仓库的 sdcard/fonts；
	@# 都用链接，不占双份空间。没整理过的原书不会自动放进模拟卡。
	@# Books on the simulated card come from ready/books (prepared by tools/prepare_books.py) and fonts from
	@# the repo's sdcard/fonts, as links that take no extra space. Unprepared originals are not added automatically.
	@for f in $(abspath $(ROOT))/ready/books/*.epub $(abspath $(ROOT))/sdcard/fonts/*.ttf; do \
		[ -f "$$f" ] || continue; d=data/sdcard/$$(basename "$$(dirname "$$f")")/$$(basename "$$f"); \
		[ -e "$$d" ] || ln -s "$$f" "$$d"; done
	@cp -n $(ROOT)/builtin/pico/books/*.epub data/flash/books/ 2>/dev/null || true
	@cp -n $(ROOT)/builtin/pico/fonts/*.ttf data/flash/fonts/ 2>/dev/null || true
	@mkdir -p data/flash/manual
	@cp -n $(ROOT)/builtin/pico/manual/* data/flash/manual/ 2>/dev/null || true

run: $(BIN) seed
	./$(BIN)

# 给编辑器的语法检查（clangd）用，一行一个编译参数。
# For editor diagnostics (clangd): one compile flag per line.
compile_flags.txt: Makefile
	@printf '%s\n' $(CFLAGS) $(SDL_CFLAGS) > $@

clean:
	rm -rf $(BUILD)

-include $(FW_OBJS:.o=.d) $(SIM_OBJS:.o=.d)

.PHONY: all run clean
