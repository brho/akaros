#ifndef _ROS_ARCH_MMU_H
#define _ROS_ARCH_MMU_H

#ifndef __ASSEMBLER__
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

// All physical memory mapped at this address
#define KERNBASE        0xC0000000

// Use this if needed in annotations
#define IVY_KERNBASE (0xC000U << 16)

#define PTSHIFT 22
#define PTSIZE (1 << PTSHIFT)

#define PGSHIFT 12
#define PGSIZE (1 << PGSHIFT)

#define JPGSIZE PTSIZE

#endif
