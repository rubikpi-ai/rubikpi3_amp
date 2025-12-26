ARMGNU ?= aarch64-linux-gnu

COPS += -g -Wall -nostdlib -nostdinc -fno-builtin -Iinclude
ASMOPS = -g -Iinclude

BUILD_DIR = build
SRC_DIR = src

all : rubikpi3_amp.bin

clean :
	rm -rf $(BUILD_DIR) *.bin *.map

$(BUILD_DIR)/%_c.o: $(SRC_DIR)/%.c
	mkdir -p $(@D)
	$(ARMGNU)-gcc $(COPS) -MMD -c $< -o $@

$(BUILD_DIR)/%_s.o: $(SRC_DIR)/%.S
	$(ARMGNU)-gcc $(ASMOPS) -MMD -c $< -o $@

C_FILES = $(wildcard $(SRC_DIR)/*.c)
ASM_FILES = $(wildcard $(SRC_DIR)/*.S)
OBJ_FILES = $(C_FILES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%_c.o)
OBJ_FILES += $(ASM_FILES:$(SRC_DIR)/%.S=$(BUILD_DIR)/%_s.o)

DEP_FILES = $(OBJ_FILES:%.o=%.d)
-include $(DEP_FILES)

rubikpi3_amp.bin: $(SRC_DIR)/linker.ld $(OBJ_FILES)
	$(ARMGNU)-ld -T $(SRC_DIR)/linker.ld \
		-Map rubikpi3_amp.map --cref --print-memory-usage \
		-o $(BUILD_DIR)/rubikpi3_amp.elf $(OBJ_FILES) -e _start
	$(ARMGNU)-objcopy $(BUILD_DIR)/rubikpi3_amp.elf -O binary rubikpi3_amp.bin
