#ifndef ROS_KERN_ARCH_PMAP_H
#define ROS_KERN_ARCH_PMAP_H

void x86_cleanup_bootmem(void);
void setup_default_mtrrs(barrier_t *smp_barrier);
physaddr_t get_boot_pml4(void);
uintptr_t get_gdt64(void);

#endif /* ROS_KERN_ARCH_PMAP_H */
