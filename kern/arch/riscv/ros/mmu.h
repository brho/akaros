#ifndef ROS_INC_MMU_H
#define ROS_INC_MMU_H

// All physical memory mapped at this address
#ifdef __riscv64
# define KERNBASE       0xFFFF800000000000
# define KERN_LOAD_ADDR 0xFFFFFFFF80000000
# define NPTLEVELS                       4
# define L1PGSHIFT              (12+9+9+9)
# define L1PGSIZE        (1L << L1PGSHIFT)
# define L2PGSHIFT                (12+9+9)
# define L2PGSIZE        (1L << L2PGSHIFT)
# define L3PGSHIFT                  (12+9)
# define L3PGSIZE        (1L << L3PGSHIFT)
# define L4PGSHIFT                    (12)
# define L4PGSIZE        (1L << L4PGSHIFT)
# define PGSHIFT                 L4PGSHIFT
# define PTSIZE                   L2PGSIZE
#else
# define KERNBASE               0x80000000
# define KERN_LOAD_ADDR           KERNBASE
# define NPTLEVELS                       2
# define L1PGSHIFT                 (12+10)
# define L1PGSIZE         (1 << L1PGSHIFT)
# define L2PGSHIFT                      12
# define L2PGSIZE         (1 << L2PGSHIFT)
# define PGSHIFT                 L2PGSHIFT
# define PTSIZE                   L1PGSIZE
#endif

#define PGSIZE (1 << PGSHIFT)

#define NOVPT

#ifndef __ASSEMBLER__
typedef unsigned long pte_t;
typedef unsigned long pde_t;
#endif

#endif
