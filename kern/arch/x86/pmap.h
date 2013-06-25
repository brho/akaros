#ifndef ROS_KERN_ARCH_PMAP_H
#define ROS_KERN_ARCH_PMAP_H

void x86_cleanup_bootmem(void);
void setup_default_mtrrs(barrier_t *smp_barrier);

#endif /* ROS_KERN_ARCH_PMAP_H */
