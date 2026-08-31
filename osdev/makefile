OUT_DIR	 := output/
GRUB_DIR := $(OUT_DIR)iso/boot/grub
OBJS_DIR := $(OUT_DIR)objs/
INC_DIRS := -Iinclude -Iarch/i386

ASMSRCS  := $(shell find . -type f -name '*.S' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*" -not -path "./user*" -not -path "./demo*")
CSRCS    := $(shell find . -type f -name '*.c' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*" -not -path "./user*" -not -path "./demo*")
CXXSRCS  := $(shell find . -type f -name '*.cpp' -not -path "./$(OUT_DIR)*" -not -path "./tests*" -not -path "./tools*" -not -path "./bootloader*" -not -path "./user*" -not -path "./demo*")
COBJS    := $(patsubst %.c,   $(OBJS_DIR)%.c.o,   $(CSRCS))
CXXOBJS  := $(patsubst %.cpp, $(OBJS_DIR)%.cpp.o, $(CXXSRCS))
ASMOBJS  := $(patsubst %.S,   $(OBJS_DIR)%.S.o,   $(ASMSRCS))

# ---- user-space programs (compiled to ELF, loaded as GRUB modules) ----
USER_LD    := user/user.ld

# portal_test.elf — standalone user-mode portal RPC test.  This is the only
# user ELF the kernel loads (after a keypress confirmation in init_thread).
PORTAL_SRCS := user/portal_test.c user/userlib.c
PORTAL_ELF  := $(OUT_DIR)portal_test.elf

OBJS     := $(COBJS) $(CXXOBJS) $(ASMOBJS) $(OBJS_DIR)user/userheap.c.o

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
CFLAGS     := -std=gnu99 -ffreestanding -O2 -g -Wall -Wextra -fno-omit-frame-pointer $(INC_DIRS) $(DEPFLAGS)
CXXFLAGS   := -ffreestanding -O2 -Wall -Wextra -g -fno-exceptions -fno-rtti $(INC_DIRS) $(DEPFLAGS)
ASFLAGS    :=
LDFLAGS    := -ffreestanding -O2 -g -nostdlib

# User programs: linked as position-dependent executables at the high user
# base (see user/user.ld); -fno-pic keeps absolute addresses.
# -Iarch/i386 exposes arch_irq.h; -Iuser exposes userlib.h to user/server/*.
USER_CFLAGS := -std=gnu99 -ffreestanding -fno-builtin -fno-pic -fno-pie -nostdlib -Iinclude -Iarch/i386 -Iuser

MEMORY_MAP := $(OUT_DIR)memory_map

# The user ELFs are shipped as separate GRUB multiboot modules (see
# config/grub.cfg), NOT embedded into the kernel image.  The ISO must
# depend on them and copy them next to the kernel binary.
$(TARGET_ISO): $(TARGET_BIN) $(GRUBFILE) $(PORTAL_ELF) | $(OUT_DIR) $(GRUB_DIR)
	cp $(TARGET_BIN) $(OUT_DIR)iso/boot/myos.bin
	cp $(PORTAL_ELF) $(OUT_DIR)iso/boot/
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

# User-mode heap: built into the kernel (the ring-3 servers run from the
# kernel image) but excluded from USER_SRCS so it is not linked into the
# user hello.elf.  -fno-builtin stops GCC treating malloc/free as builtins.
$(OBJS_DIR)user/userheap.c.o: user/userheap.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -fno-builtin -c $< -o $@

$(PORTAL_ELF): $(PORTAL_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(PORTAL_SRCS) -o $@ -lgcc

$(OUT_DIR) $(GRUB_DIR) $(OBJS_DIR):
	@mkdir -p $@

all: $(TARGET_ISO)

env:
	(cd tools && ./setupenv.sh)

run: $(TARGET_ISO)
	qemu-system-i386 -cdrom $(TARGET_ISO) -serial file:serial.log

# QEMU -kernel cannot load GRUB multiboot modules, so debug from the ISO.
run_debug: $(TARGET_ISO)
	qemu-system-i386 -s -S -cdrom $(TARGET_ISO) -serial file:serial.log

# --- Crash dump: extract EIPs from serial log and resolve to function names ---
LOG ?= serial.log
DUMP_BIN := $(TARGET_BIN)
ADDR2LINE := $(ARCH)addr2line

dump: $(DUMP_BIN)
	@echo "=== Crash Stack Trace ==="
	@echo "  Log    : $(LOG)"
	@echo "  Binary : $(DUMP_BIN)"
	@echo "  Tool   : $(ADDR2LINE)"
	@echo ""
	@if [ ! -f $(LOG) ]; then \
		echo "Error: '$(LOG)' not found.  Run 'make run' first."; \
		exit 1; \
	fi
	@if ! command -v $(ADDR2LINE) >/dev/null 2>&1; then \
		echo "Error: '$(ADDR2LINE)' not found in PATH."; \
		echo "  You may need to source your cross-compiler environment"; \
		echo "  or set PATH to include the i686-elf bin directory."; \
		exit 1; \
	fi
	@crash=$$(sed -n '/--- STACK_TRACE_BEGIN ---/,/--- STACK_TRACE_END ---/p' $(LOG) | \
	           grep -o 'CRASH:[0-9A-Fa-f]\{8\}' | sed 's/CRASH://' | tr -d '\r'); \
	eips=$$(sed -n '/--- STACK_TRACE_BEGIN ---/,/--- STACK_TRACE_END ---/p' $(LOG) | \
	         grep -o 'EIP:[0-9A-Fa-f]\{8\}' | sed 's/EIP://' | tr -d '\r'); \
	if [ -n "$$crash" ]; then \
		line=$$($(ADDR2LINE) -e $(DUMP_BIN) -f -p -C "0x$$crash" 2>&1); \
		echo "  >>> CRASH at 0x$$crash  $$line"; \
		echo ""; \
	fi; \
	if [ -z "$$eips" ]; then \
		echo "  (no call-chain EIPs found)"; \
	else \
		echo "  Call chain (return addresses):"; \
		i=0; \
		for addr in $$eips; do \
			line=$$($(ADDR2LINE) -e $(DUMP_BIN) -f -p -C "0x$$addr" 2>&1); \
			if [ -n "$$line" ] && [ "$$line" != "?? ??:0" ]; then \
				echo "  #$$i  0x$$addr  $$line"; \
			else \
				echo "  #$$i  0x$$addr  (not resolved)"; \
			fi; \
			i=$$((i+1)); \
		done; \
	fi
	@echo ""

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

.PHONY: all env run run_boot clean dump

