SOURCE   = source/
OUTPUT	 = output/
LINKFILE = $(SOURCE)linker.ld
TARGET   = $(OUTPUT)myos.bin
OBJS     = $(OUTPUT)kernel.o $(OUTPUT)terminal.o $(OUTPUT)boot.o

CC 		:= i686-elf-gcc
CPP		:= i686-elf-g++
AS 		:= i686-elf-as
LD 		:= $(CC) -T $(LINKFILE)

CFLAGS  := -std=gnu99 -ffreestanding -O2 -Wall -Wextra
CPPFLAGS:= -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti
ASFLAGS :=
LDFLAGS := -ffreestanding -O2 -nostdlib -lgcc

all: $(TARGET) POSTBUILD
$(shell mkdir -p output)

$(TARGET) : $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.c
	$(CC) $(CFLAGS) -c $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.cpp
	$(CPP) $(CPPFLAGS) -c $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.s
	$(AS) $(ASFLAGS) $^ -o $@

POSTBUILD:
	mkdir -p $(OUTPUT)iso/boot/grub
	cp $(TARGET) $(OUTPUT)iso/boot/myos.bin
	cp $(SOURCE)grub.cfg $(OUTPUT)iso/boot/grub/grub.cfg
	grub-mkrescue -o $(OUTPUT)myos.iso $(OUTPUT)iso

clean:
	rm -rf $(OUTPUT)

.PHONY: clean

