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

# namespace_server.elf — name -> {portal_id, mailbox_tid, mail_magic}
# registry served at the fixed PORTAL_ID_NAMESPACE portal.
NS_SRCS := user/server/ns/namespace_server.c user/userlib.c
NS_ELF  := $(OUT_DIR)namespace_server.elf

# portal_test.elf — standalone user-mode portal RPC test.
PORTAL_SRCS := user/portal_test.c user/userlib.c
PORTAL_ELF  := $(OUT_DIR)portal_test.elf

# driver-server ELFs — standalone ring-3 drivers (loaded as GRUB modules)
TERMINAL_SRCS := user/server/display/terminal_server.c user/userlib.c user/server/user_service.c
TERMINAL_ELF  := $(OUT_DIR)terminal_server.elf
KB_SRCS       := user/server/input/kb_server.c user/userlib.c user/server/user_service.c
KB_ELF        := $(OUT_DIR)kb_server.elf

# log_server2.elf — user-mode LOG server: registers "log" in the namespace
# and writes portal_call() payloads to COM1 via ioread8/iowrite8
# (user_service.c) through the SYSCALL_IO gate.
LOG_SRCS := user/server/serial/log_server2.c user/userlib.c user/server/user_service.c
LOG_ELF  := $(OUT_DIR)log_server2.elf

# rtc_server.elf — user-mode RTC/sleep server: registers "rtc" in the
# namespace; GET_TIME reads the CMOS RTC (0x70-0x71), SLEEP_MS spins on
# the latched PIT channel-0 counter (0x40-0x43), both via SYSCALL_IO.
RTC_SRCS := user/server/clock/rtc_server.c user/userlib.c user/server/user_service.c
RTC_ELF  := $(OUT_DIR)rtc_server.elf

# hello.elf — first user ELF demo (console portal + log portal via userlib).
HELLO_SRCS := user/hello.c user/userlib.c
HELLO_ELF  := $(OUT_DIR)hello.elf

# ipc_bench.elf — IPC ping-pong latency benchmark (TSC timing; CAP_IPC only).
IPCBENCH_SRCS := user/ipc_bench.c user/userlib.c
IPCBENCH_ELF  := $(OUT_DIR)ipc_bench.elf

# process_test.elf — keyboard-driven text-mode test menu.  The ported demo
# suites run as threads of this process; kernel/lib/{rbtree,string,spinlock}
# are linked in so the suites keep their kernel-era code.  userlib provides
# malloc/free (SYSCALL_HEAP) for the suites' kmalloc/kfree shims.
PROCESS_TEST_SRCS := user/demo/process_test_entry.c \
	user/demo/demo_common.c \
	user/demo/thread_api_test.c \
	user/demo/process_api_test.c \
	user/demo/sched_mix_test.c \
	user/demo/rbtree_test.c \
	user/demo/demo_kmalloc.c \
	kernel/lib/rbtree.c \
	kernel/lib/string.c \
	kernel/sync/spinlock.c \
	user/userlib.c
PROCESS_TEST_ELF := $(OUT_DIR)process_test.elf

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
$(TARGET_ISO): $(TARGET_BIN) $(GRUBFILE) $(NS_ELF) $(PORTAL_ELF) $(TERMINAL_ELF) $(KB_ELF) $(LOG_ELF) $(RTC_ELF) $(HELLO_ELF) $(PROCESS_TEST_ELF) $(IPCBENCH_ELF) | $(OUT_DIR) $(GRUB_DIR)
	cp $(TARGET_BIN) $(OUT_DIR)iso/boot/myos.bin
	cp $(NS_ELF) $(OUT_DIR)iso/boot/
	cp $(PORTAL_ELF) $(OUT_DIR)iso/boot/
	cp $(TERMINAL_ELF) $(OUT_DIR)iso/boot/
	cp $(KB_ELF) $(OUT_DIR)iso/boot/
	cp $(LOG_ELF) $(OUT_DIR)iso/boot/
	cp $(RTC_ELF) $(OUT_DIR)iso/boot/
	cp $(HELLO_ELF) $(OUT_DIR)iso/boot/
	cp $(IPCBENCH_ELF) $(OUT_DIR)iso/boot/
	cp $(PROCESS_TEST_ELF) $(OUT_DIR)iso/boot/
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

# --- user-heap syscall lives in kernel/mm/heap.c (SYSCALL_HEAP); the old
# ring-3 static userheap.c is gone.  -fno-builtin stops GCC treating
# malloc/free as builtins in USER_CFLAGS below.

$(NS_ELF): $(NS_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(NS_SRCS) -o $@ -lgcc

$(PORTAL_ELF): $(PORTAL_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(PORTAL_SRCS) -o $@ -lgcc

$(TERMINAL_ELF): $(TERMINAL_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(TERMINAL_SRCS) -o $@ -lgcc

$(KB_ELF): $(KB_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(KB_SRCS) -o $@ -lgcc

$(LOG_ELF): $(LOG_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(LOG_SRCS) -o $@ -lgcc

$(RTC_ELF): $(RTC_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(RTC_SRCS) -o $@ -lgcc

$(HELLO_ELF): $(HELLO_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(HELLO_SRCS) -o $@ -lgcc

$(IPCBENCH_ELF): $(IPCBENCH_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(IPCBENCH_SRCS) -o $@ -lgcc

$(PROCESS_TEST_ELF): $(PROCESS_TEST_SRCS) $(USER_LD) | $(OUT_DIR)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) $(PROCESS_TEST_SRCS) -o $@ -lgcc

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

