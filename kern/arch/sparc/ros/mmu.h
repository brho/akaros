#ifndef _ROS_ARCH_MMU_H
#define _ROS_ARCH_MMU_H

// All physical memory mapped at this address
#define KERNBASE        0x80000000

// Use this if needed in annotations
#define IVY_KERNBASE (0x8000U << 16)

#define PGSHIFT 12
#define PGSIZE (1 << PGSHIFT)
#define PTSIZE PGSIZE

#endif
