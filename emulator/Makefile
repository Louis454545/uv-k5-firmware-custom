ROOT := ..
BUILD := build
TARGET := $(BUILD)/uvk5-emulator

CFLAGS := -O2 -g -std=gnu11 -fshort-enums -fno-delete-null-pointer-checks -MMD -include stdbool.h \
	-Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations \
	-DHOST_EMULATOR -DPRINTF_INCLUDE_CONFIG_H \
	-DAUTHOR_STRING=\"EGZUMER+PBA\" -DVERSION_STRING=\"v2.4\" \
	-DENABLE_SPECTRUM -DENABLE_FMRADIO -DENABLE_UART -DENABLE_BIG_FREQ \
	-DENABLE_SMALL_BOLD -DENABLE_TX1750 -DENABLE_KEEP_MEM_NAME -DENABLE_WIDE_RX \
	-DENABLE_NO_CODE_SCAN_TIMEOUT -DENABLE_AM_FIX -DENABLE_SQUELCH_MORE_SENSITIVE=1 \
	-DENABLE_FASTER_CHANNEL_SCAN -DENABLE_RSSI_BAR -DENABLE_AUDIO_BAR \
	-DENABLE_COPY_CHAN_TO_VFO -DENABLE_SCAN_RANGES -DENABLE_FLASHLIGHT \
	-DENABLE_CUSTOM_MENU_LAYOUT -DENABLE_FEAT_F4HWN -DALERT_TOT=10 \
	-DAUTHOR_STRING_1=\"EGZUMER\" -DVERSION_STRING_1=\"v0.22\" \
	-DAUTHOR_STRING_2=\"PBA\" -DVERSION_STRING_2=\"v2.4\" \
	-DENABLE_FEAT_F4HWN_PMR \
	-Iinclude -I$(ROOT) -I$(ROOT)/emulator/include -Isrc \
	-I$(ROOT)/external/CMSIS_5/CMSIS/Core/Include \
	-I$(ROOT)/external/CMSIS_5/Device/ARM/ARMCM0/Include \
	$(shell sdl2-config --cflags)

HOST_CFLAGS := $(filter-out -fshort-enums,$(CFLAGS))

LDLIBS := $(shell sdl2-config --libs) -lcrypto -lpthread -lm

FW_SRCS := \
	external/printf/printf.c \
	driver/backlight.c \
	driver/spi.c \
	driver/st7565.c \
	app/action.c \
	app/app.c \
	app/chFrScanner.c \
	app/common.c \
	app/dtmf.c \
	app/flashlight.c \
	app/fm.c \
	app/generic.c \
	app/main.c \
	app/menu.c \
	app/scanner.c \
	app/spectrum.c \
	app/uart.c \
	am_fix.c \
	audio.c \
	bitmaps.c \
	board.c \
	dcs.c \
	font.c \
	frequencies.c \
	functions.c \
	helper/battery.c \
	helper/boot.c \
	misc.c \
	radio.c \
	scheduler.c \
	settings.c \
	ui/battery.c \
	ui/fmradio.c \
	ui/helper.c \
	ui/inputbox.c \
	ui/main.c \
	ui/menu.c \
	ui/scanner.c \
	ui/status.c \
	ui/ui.c \
	ui/welcome.c \
	version.c \
	main.c

HOST_SRCS := \
	src/host_adc.c \
	src/host_aes.c \
	src/host_bk1080.c \
	src/host_crc.c \
	src/host_eeprom.c \
	src/host_hw.c \
	src/host_i2c.c \
	src/host_input.c \
	src/host_main.c \
	src/host_radio.c \
	src/host_system.c \
	src/host_systick.c \
	src/host_uart.c

OBJS := $(FW_SRCS:%.c=$(BUILD)/fw/%.o) $(HOST_SRCS:%.c=$(BUILD)/host/%.o) $(BUILD)/generated/bk4819_host.o
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ -Wl,--wrap=APP_Update $(LDLIBS)

$(BUILD)/fw/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/generated/bk4819_host.c: $(ROOT)/driver/bk4819.c tools/make_bk4819_host.py
	@mkdir -p $(dir $@)
	python3 tools/make_bk4819_host.py $< $@

$(BUILD)/generated/bk4819_host.o: $(BUILD)/generated/bk4819_host.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(ROOT) -c $< -o $@

$(BUILD)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

run: $(TARGET)
	./$(TARGET)

-include $(DEPS)

.PHONY: all clean run
