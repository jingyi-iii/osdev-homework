SOURCE      = source/
OUTPUT	    = output/
FD          = $(OUTPUT)bl.img
TARGET      = $(OUTPUT)bootloader.bin
OBJS        = $(OUTPUT)bootloader.o

CC          := i686-elf-gcc
CFLAGS      :=
AS          := i686-elf-as
ASFLAGS     :=
LD          := i686-elf-ld
LDFLAGS     := -Ttext=0x7c00 --oformat binary

all: $(TARGET) POSTBUILD
$(shell mkdir -p output)

$(TARGET) : $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.S
	$(AS) $(ASFLAGS) $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.c
	$(CC) $(CFLAGS) -c $^ -o $@

POSTBUILD:
	bximage -func=create -fd=1.44M -q $(FD)
	dd if=$(TARGET) of=$(FD) bs=512 count=1 conv=notrunc
	qemu-system-x86_64 -fda $(FD)

clean:
	rm -rf $(OUTPUT)

.PHONY: clean

