ifeq ($(CONFIG_X86_64),y)
CROSS_COMPILE := x86_64-ros-
BITS := 64
KERNEL_LD := kernel64.ld
CFLAGS_KERNEL += -mcmodel=kernel
CFLAGS_KERNEL += -mno-red-zone -ffreestanding
CFLAGS_KERNEL += -mno-mmx -mno-sse -mno-sse2 -mno-sse3 -mno-3dnow
LDFLAGS_KERNEL += -z max-page-size=0x1000
OBJDUMP_FLAGS += -M x86-64
else
CROSS_COMPILE := i686-ros-
BITS := 32
KERNEL_LD := kernel32.ld
endif

# BITS is used in Kbuilds, so we can select bit-specific source files.
export BITS
