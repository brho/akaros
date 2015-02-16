CROSS_COMPILE := x86_64-ucb-akaros-
BITS := 64
KERNEL_LD := kernel64.ld
CFLAGS_KERNEL += -mcmodel=kernel
CFLAGS_KERNEL += -mno-red-zone -ffreestanding
CFLAGS_KERNEL += -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-3dnow
LDFLAGS_KERNEL += -z max-page-size=0x1000
# Need to change the format to 32 bit, to trick multiboot/grub1 into loading
ARCH_POST_LINK_CMD = cp $@ $@-64b; $(OBJCOPY) -I elf64-x86-64 -O elf32-i386 $@

# BITS is used in Kbuilds, so we can select bit-specific source files.
export BITS
