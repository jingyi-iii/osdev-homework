SRC_DIR  := source/
OUT_DIR	 := output/
GRUB_DIR := $(OUT_DIR)iso/boot/grub
OBJS_DIR := $(OUT_DIR)objs/
VLD_DIRS := $(filter-out ./.git% ./$(OUT_DIR)%, $(shell find $(SRC_DIR) -type d))
SRC_DIRS := $(patsubst ./%,%,$(VLD_DIRS))
INC_DIRS := $(addprefix -I,$(SRC_DIRS))

ASMSRCS  := $(shell find . -type f -name '*.S' -not -path "./$(OUT_DIR)*")
CSRCS    := $(shell find . -type f -name '*.c' -not -path "./$(OUT_DIR)*")
CXXSRCS  := $(shell find . -type f -name '*.cpp' -not -path "./$(OUT_DIR)*")
COBJS    := $(patsubst %.c,   $(OBJS_DIR)%.c.o,   $(CSRCS))
CXXOBJS  := $(patsubst %.cpp, $(OBJS_DIR)%.cpp.o, $(CXXSRCS))
ASMOBJS  := $(patsubst %.S,   $(OBJS_DIR)%.S.o,   $(ASMSRCS))
OBJS     := $(COBJS) $(CXXOBJS) $(ASMOBJS)

TARGET_BIN := $(OUT_DIR)myos.bin
TARGET_ISO := $(OUT_DIR)myos.iso
LINKFILE   := $(SRC_DIR)linker.ld
GRUBFILE   := $(SRC_DIR)grub.cfg
ARCH       := i686-elf-

CC         := $(ARCH)gcc
CXX        := $(ARCH)g++
AS         := $(ARCH)as

DEPFLAGS   := -MMD -MP
CFLAGS     := -std=gnu99 -ffreestanding -O2 -Wall -Wextra $(INC_DIRS) $(DEPFLAGS)
CXXFLAGS   := -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti $(INC_DIRS) $(DEPFLAGS)
ASFLAGS    :=
LDFLAGS    := -ffreestanding -O2 -nostdlib

$(TARGET_ISO): $(TARGET_BIN) $(GRUBFILE) | $(OUT_DIR) $(GRUB_DIR)
	cp $(TARGET_BIN) $(OUT_DIR)iso/boot/myos.bin
	cp $(GRUBFILE) $(GRUB_DIR)
	grub-mkrescue -o $(TARGET_ISO) $(OUT_DIR)iso

$(TARGET_BIN): $(OBJS)
	$(CC) -T $(LINKFILE) $(LDFLAGS) $^ -o $@ -lgcc

$(OBJS_DIR)%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_DIR)%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS_DIR)%.S.o: %.S
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(OUT_DIR) $(GRUB_DIR):
	@mkdir -p $@

all: $(TARGET_ISO)

env:
	(cd tools && ./setupenv.sh)

run: $(TARGET_ISO)
	qemu-system-i386 -cdrom $(TARGET_ISO) -serial file:serial.log

clean:
	@rm -rf $(OUT_DIR) 

.PHONY: all env run clean

