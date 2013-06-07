#ifndef ROS_INC_PERFMON_H
#define ROS_INC_PERFMON_H
#include <ros/common.h>
#include <arch/x86.h>

#define IA32_PMC_BASE 0xC1
#define IA32_PERFEVTSEL_BASE 0x186

#define LLCACHE_EVENT 0x2E
#define LLCACHE_MISS_MASK 0x41
#define LLCACHE_REF_MASK 0x4F
#define ENABLE_PERFCTR 0x00400000
#define DISABLE_PERFCTR 0xFFAFFFFF


static __inline uint64_t
read_pmc(uint32_t index)
{                                                                                                    
    uint64_t pmc;

    __asm __volatile("rdpmc" : "=A" (pmc) : "c" (index)); 
    return pmc;                                                                                      
}

void perfmon_init();

#endif /* ROS_INC_PERFMON_H */
