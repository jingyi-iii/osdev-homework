OUT_DIR	 := output/
GRUB_DIR := $(OUT_DIR)iso/boot/grub
OBJS_DIR := $(OUT_DIR)objs/
INC_DIRS := -Iinclude -Iarch/i386

ASMSRCS  := $(shell find . -type f -name '*.S' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*")
CSRCS    := $(shell find . -type f -name '*.c' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*")
CXXSRCS  := $(shell find . -type f -name '*.cpp' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*")
COBJS    := $(patsubst %.c,   $(OBJS_DIR)%.c.o,   $(CSRCS))
CXXOBJS  := $(patsubst %.cpp, $(OBJS_DIR)%.cpp.o, $(CXXSRCS))
ASMOBJS  := $(patsubst %.S,   $(OBJS_DIR)%.S.o,   $(ASMSRCS))
OBJS     := $(COBJS) $(CXXOBJS) $(ASMOBJS)

TARGET_BIN := $(OUT_DIR)myos.bin
TARGET_ISO := $(OUT_DIR)myos.iso
LINKFILE   := config/linker.ld
GRUBFILE   := config/grub.cfg
ARCH       := i686-elf-

CC         := $(ARCH)gcc
CXX        := $(ARCH)g++
AS         := $(ARCH)as
OBJDUMP    := $(ARCH)objdump
NM         := $(ARCH)nm
SIZE       := $(ARCH)size

DEPFLAGS   := -MMD -MP
CFLAGS     := -std=gnu99 -ffreestanding -O2 -g -Wall -Wextra $(INC_DIRS) $(DEPFLAGS)
CXXFLAGS   := -ffreestanding -O2 -Wall -Wextra -g -fno-exceptions -fno-rtti $(INC_DIRS) $(DEPFLAGS)
ASFLAGS    :=
LDFLAGS    := -ffreestanding -O2 -g -nostdlib

MEMORY_MAP := $(OUT_DIR)memory_map

$(TARGET_ISO): $(TARGET_BIN) $(GRUBFILE) | $(OUT_DIR) $(GRUB_DIR)
	cp $(TARGET_BIN) $(OUT_DIR)iso/boot/myos.bin
	cp $(GRUBFILE) $(GRUB_DIR)
	grub-mkrescue -o $(TARGET_ISO) $(OUT_DIR)iso

$(TARGET_BIN): $(OBJS)
	$(CC) -T $(LINKFILE) $(LDFLAGS) $^ -o $@ -lgcc
	@echo "  GEN     $(MEMORY_MAP)"
	@$(OBJDUMP) -h $@ > $(MEMORY_MAP)
	@echo "" >> $(MEMORY_MAP)
	@echo "=== Section Sizes ===" >> $(MEMORY_MAP)
	@$(SIZE) $@ >> $(MEMORY_MAP)
	@echo "" >> $(MEMORY_MAP)
	@echo "=== Symbols by Address ===" >> $(MEMORY_MAP)
	@$(NM) -n $@ >> $(MEMORY_MAP)

$(OBJS_DIR)%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_DIR)%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS_DIR)%.S.o: %.S
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(OUT_DIR) $(GRUB_DIR) $(OBJS_DIR):
	@mkdir -p $@

all: $(TARGET_ISO)

env:
	(cd tools && ./setupenv.sh)

run: $(TARGET_ISO)
	qemu-system-i386 -cdrom $(TARGET_ISO) -serial file:serial.log

run_debug: $(TARGET_BIN)
	qemu-system-i386 -s -S -kernel $(TARGET_BIN) -serial file:serial.log

# --- Simple bootloader (no GRUB, bare metal) ---
BOOTLOADER_BIN := $(OUT_DIR)bootloader.bin

$(BOOTLOADER_BIN): bootloader/boot.S bootloader/linker.ld | $(OUT_DIR) $(OBJS_DIR)
	$(AS) --32 $< -o $(OBJS_DIR)bootloader.o
	$(ARCH)ld -m elf_i386 -T bootloader/linker.ld $(OBJS_DIR)bootloader.o -o $@

run_boot: $(BOOTLOADER_BIN)
	qemu-system-i386 -drive format=raw,file=$(BOOTLOADER_BIN)

clean:
	@rm -rf $(OUT_DIR)
	@rm -rf *.log

.PHONY: all env run run_boot clean

