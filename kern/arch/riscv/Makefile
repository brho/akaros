CROSS_COMPILE := riscv-ucb-akaros-
# basically a nop.  this runs post-link of the kernel
ARCH_POST_LINK_CMD = echo -n

# Machinery to create the kernel build info source
KERNEL_ELF_PATH := $(abspath $(KERNEL_OBJ))
export KERNEL_ELF_PATH
