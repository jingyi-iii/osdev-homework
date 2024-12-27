OUTPUT	 = output/
SOURCE   = $(shell find . -type d)

SSRCS    = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.S))
CSRCS    = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.c))
CPPSRCS  = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.cpp))
COBJS    = $(patsubst %.c,   %_c.o,   $(CSRCS))
CPPOBJS  = $(patsubst %.cpp, %_cpp.o, $(CPPSRCS))
SOBJS    = $(patsubst %.S,   %_S.o,   $(SSRCS))
OBJS     = $(COBJS) $(CPPOBJS) $(SOBJS)

TARGET   = $(OUTPUT)myos.bin
LINKFILE = source/linker.ld
GRUBFILE = source/grub.cfg
INCLUDE  = -I source/boot/ -I source/core -I source/io/

CC 		:= i686-elf-gcc
CPP		:= i686-elf-g++
AS 		:= i686-elf-as
LD 		:= $(CC) -T $(LINKFILE)

CFLAGS  := -std=gnu99 -ffreestanding -O2 -Wall -Wextra $(INCLUDE)
CPPFLAGS:= -ffreestanding -O2 -Wall -Wextra -fno-exceptions -fno-rtti $(INCLUDE)
ASFLAGS :=
LDFLAGS := -ffreestanding -O2 -nostdlib -lgcc


all: $(TARGET) POSTBUILD
$(shell mkdir -p output)

$(TARGET) : $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@
%_c.o : %.c
	$(CC) $(CFLAGS) -c $^ -o $@
%_cpp.o : %.cpp
	$(CPP) $(CPPFLAGS) -c $^ -o $@
%_S.o : %.S
	$(AS) $(ASFLAGS) $^ -o $@

POSTBUILD:
	rm $(OBJS)
	mkdir -p $(OUTPUT)iso/boot/grub
	cp $(TARGET) $(OUTPUT)iso/boot/myos.bin
	cp $(GRUBFILE) $(OUTPUT)iso/boot/grub/
	grub-mkrescue -o $(OUTPUT)myos.iso $(OUTPUT)iso
	qemu-system-i386 -cdrom $(OUTPUT)myos.iso

clean:
	rm -rf $(OUTPUT) 

.PHONY: clean

