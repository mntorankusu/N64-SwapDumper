all: sharkdumper.z64
.PHONY: all
.SECONDARY:

BUILD_DIR := build
SOURCE_DIR := src
include $(N64_INST)/include/n64.mk

OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/pif.o $(BUILD_DIR)/accessory.o

N64_CFLAGS += -std=gnu2x -Os -G0

sharkdumper.z64: N64_ROM_TITLE = "SharkDumper"

$(BUILD_DIR)/sharkdumper.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64
.PHONY: clean

-include $(wildcard $(BUILD_DIR)/*.d)
