# Turbo Touch screen simulator -- builds the real UI against host LVGL.
# Requires: gcc, make. No ESP-IDF, no SDL, no emscripten.

ROOT    := ..
LVGL    := $(ROOT)/components/lvgl__lvgl
MAIN    := $(ROOT)/main
BUILD   := build
TARGET  := gtsim

# Excluded from main/: the real panel driver, the ESP LVGL port and the
# firmware entry point -- sim_main.c replaces all three.
MAIN_EXCLUDE := $(MAIN)/main.c $(MAIN)/lvgl_port.c $(MAIN)/waveshare_rgb_lcd_port.c \
                $(MAIN)/ota_update.c $(MAIN)/ota_screen.c $(MAIN)/bap_uart.c $(MAIN)/bap_client.c

MAIN_SRC  := $(filter-out $(MAIN_EXCLUDE),$(wildcard $(MAIN)/*.c))
# logo_background.c is #included directly by loading.c, so never compile it alone.
ASSET_SRC := $(filter-out $(MAIN)/assets/logo_background.c,$(wildcard $(MAIN)/assets/*.c))
LVGL_SRC  := $(shell find $(LVGL)/src -name '*.c')
SIM_SRC   := sim_main.c sim_rt.c sim_shims.c

SRC := $(MAIN_SRC) $(ASSET_SRC) $(LVGL_SRC) $(SIM_SRC)
# Strip the leading ../ so objects stay under build/ instead of collapsing back
# out into the source tree (build/../main/foo.o is just main/foo.o).
OBJ := $(patsubst %.c,$(BUILD)/%.o,$(patsubst ../%,%,$(SRC)))

INC := -Istubs -I$(MAIN) -I$(ROOT)/components -I$(LVGL) -I$(LVGL)/src

CFLAGS := -O1 -g -std=gnu11 -DLV_CONF_INCLUDE_SIMPLE $(INC) \
          -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable \
          -Wno-format -Wno-format-truncation -Wno-implicit-fallthrough \
          -Wno-missing-braces -Wno-incompatible-pointer-types -Wno-int-conversion \
          -Wno-discarded-qualifiers -Wno-sign-compare
LDFLAGS := -lm -lpthread

.PHONY: all clean run
all: $(TARGET)

$(TARGET): $(OBJ)
	@echo "  LD   $@"
	@gcc $(OBJ) -o $@ $(LDFLAGS)

$(BUILD)/%.o: ../%.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@gcc $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC   $<"
	@gcc $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET)

run: $(TARGET)
	python3 server.py
