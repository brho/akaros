#ifndef _ROS_ARCH_MMU_H
#define _ROS_ARCH_MMU_H

// All physical memory mapped at this address
#ifdef __riscv64
# define KERNBASE  0xFFFFFF8000000000
# define NLEVELS                    4
# define L1PGSHIFT         (12+9+9+9)
# define L1PGSIZE    (1 << L1PGSHIFT)
# define L2PGSHIFT           (12+9+9)
# define L2PGSIZE    (1 << L2PGSHIFT)
# define L3PGSHIFT             (12+9)
# define L3PGSIZE    (1 << L3PGSHIFT)
# define L4PGSHIFT               (12)
# define L4PGSIZE    (1 << L4PGSHIFT)
# define PGSHIFT            L4PGSHIFT
# define KPGSHIFT           L3PGSHIFT
#else
# define KERNBASE          0x80000000
# define NLEVELS                    2
# define L1PGSHIFT            (12+10)
# define L1PGSIZE    (1 << L1PGSHIFT)
# define L2PGSHIFT                 12
# define L2PGSIZE    (1 << L2PGSHIFT)
# define PGSHIFT            L2PGSHIFT
# define KPGSHIFT           L1PGSHIFT
#endif

#define PGSIZE (1 << PGSHIFT)
#define KPGSIZE (1 << KPGSHIFT)
#define PTSIZE PGSIZE

#ifndef __ASSEMBLER__
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

#endif
