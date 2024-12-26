SOURCE          = source/
OUTPUT	        = output/
INTEL-TARGET    = $(OUTPUT)bl_intel.img
INTEL-OBJS      = $(OUTPUT)bootloader.intel
ATT-TARGET      = $(OUTPUT)bl_att.img
ATT-OBJS        = $(OUTPUT)bootloader.att

INTEL-AS        := nasm
INTEL-ASFLAGS   :=
ATT-AS          := i686-elf-as
ATT-ASFLAGS     :=
ATT-LD          := i686-elf-ld
ATT-LDFLAGS     := -Ttext=0x7c00 --oformat binary

intel: $(INTEL-TARGET) INTEL-POSTBUILD
$(shell mkdir -p output)

$(INTEL-TARGET) : $(INTEL-OBJS)
	bximage -func=create -fd=1.44M -q $(INTEL-TARGET)

att: $(ATT-TARGET) ATT-POSTBUILD
$(shell mkdir -p output)

$(ATT-TARGET) : $(ATT-OBJS)
	bximage -func=create -fd=1.44M -q $(ATT-TARGET)


$(OUTPUT)%.intel : $(SOURCE)%.asm
	$(INTEL-AS) $(INTEL-ASFLAGS) $^ -o $@
$(OUTPUT)%.o : $(SOURCE)%.S
	$(ATT-AS) $(ATT-ASFLAGS) $^ -o $@
$(OUTPUT)%.att : $(OUTPUT)%.o
	$(ATT-LD) $(ATT-LDFLAGS) $^ -o $@

INTEL-POSTBUILD:
	dd if=$(INTEL-OBJS) of=$(INTEL-TARGET) bs=512 count=1 conv=notrunc
	qemu-system-x86_64 -fda $(INTEL-TARGET)

ATT-POSTBUILD:
	dd if=$(ATT-OBJS) of=$(ATT-TARGET) bs=512 count=1 conv=notrunc
	qemu-system-x86_64 -fda $(ATT-TARGET)

clean:
	rm -rf $(OUTPUT)

.PHONY: clean

