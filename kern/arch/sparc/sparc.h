#ifndef ROS_INC_SPARC_H
#define ROS_INC_SPARC_H

#define CORE_ID_REG	%asr15
#define NUM_CORES_REG	%asr14
#define MEMSIZE_MB_REG	%asr13

#define PSR_CWP		0x0000001F
#define PSR_ET		0x00000020
#define PSR_PS		0x00000040
#define PSR_S		0x00000080
#define PSR_PIL		0x00000F00
#define PSR_EF		0x00001000
#define PSR_EC		0x00002000
#define PSR_RESERVED	0x000FC000
#define PSR_ICC		0x00F00000
#define PSR_VER		0x0F000000
#define PSR_IMPL	0xF0000000

#ifndef __ASSEMBLER__

#define STR(arg) #arg
#define XSTR(arg) STR(arg)

#include <ros/common.h>
#include <trap.h>
#include <arch/frontend.h>

static __inline uint32_t read_psr(void) __attribute__((always_inline));
static __inline uint32_t read_wim(void) __attribute__((always_inline));
static __inline uint32_t read_tbr(void) __attribute__((always_inline));
static __inline uint32_t read_mmu_reg(uint32_t which) __attribute__((always_inline));
static __inline uint32_t read_y(void) __attribute__((always_inline));
static __inline uint32_t read_fsr(void) __attribute__((always_inline));
static __inline uint64_t read_perfctr(uint32_t which) __attribute__((always_inline));
static __inline void write_psr(uint32_t val) __attribute__((always_inline));
static __inline void write_wim(uint32_t val) __attribute__((always_inline));
static __inline void write_tbr(uint32_t val) __attribute__((always_inline));
static __inline void write_mmu_reg(uint32_t which, uint32_t val) __attribute__((always_inline));
static __inline void write_y(uint32_t val) __attribute__((always_inline));
static __inline void write_fsr(uint32_t val) __attribute__((always_inline));
static __inline uint32_t memsize_mb(void) __attribute__((always_inline));
static __inline uint32_t mmu_probe(uint32_t va) __attribute__((always_inline));

void flush_windows();

#define store_alternate(addr,asi,data) ({ uint32_t __my_addr = (addr); uint32_t __my_data = (data); __asm__ __volatile__ ("sta %0,[%1] %2" : : "r"(__my_data),"r"(__my_addr),"i"(asi)); })
#define load_alternate(addr,asi) ({ uint32_t __my_addr = (addr); uint32_t __my_data; __asm__ __volatile__ ("lda [%1] %2,%0" : "=r"(__my_data) : "r"(__my_addr),"i"(asi)); __my_data; })

static __inline uint32_t
read_psr(void)
{
	uint32_t reg;
	asm volatile ("mov %%psr,%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
read_wim(void)
{
	uint32_t reg;
	asm volatile ("mov %%wim,%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
read_tbr(void)
{
	uint32_t reg;
	asm volatile ("mov %%tbr,%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
read_mmu_reg(uint32_t which)
{
	return load_alternate(which,4);
}

static __inline uint32_t
read_y(void)
{
	uint32_t reg;
	asm volatile ("mov %%y,%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
read_fsr(void)
{
	uint32_t reg;
	asm volatile ("st %%fsr,%0" : "=m"(reg));
	return reg;
}

static __inline uint64_t
read_perfctr(uint32_t which)
{
	uint32_t hi,lo;
	intptr_t addr = which<<3;
	hi = load_alternate(addr,2);
	lo = load_alternate(addr+4,2);
	return (((uint64_t)hi) << 32) | lo;
}

static __inline void
write_psr(uint32_t val)
{
	asm volatile ("mov %0,%%psr; nop;nop;nop" : : "r"(val) : "memory");
}

static __inline void
write_wim(uint32_t val)
{
	asm volatile ("mov %0,%%wim; nop;nop;nop" : : "r"(val) : "memory");
}

static __inline void
write_tbr(uint32_t val)
{
	asm volatile ("mov %0,%%tbr; nop;nop;nop" : : "r"(val) : "memory");
}

static __inline void
write_mmu_reg(uint32_t which, uint32_t val)
{
	store_alternate(which,4,val);
}

static __inline void
write_y(uint32_t val)
{
	asm volatile ("mov %0,%%y; nop;nop;nop" : : "r"(val) : "memory");
}

static __inline void
write_fsr(uint32_t val)
{
	asm volatile ("ld %0,%%fsr; nop;nop;nop" : : "m"(val) : "memory");
}

static __inline uint32_t
memsize_mb(void)
{
	uint32_t reg;
	__asm__ __volatile__("mov %" XSTR(MEMSIZE_MB_REG) ",%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
num_cores(void)
{
	uint32_t reg;
	__asm__ __volatile__("mov %" XSTR(NUM_CORES_REG) ",%0" : "=r"(reg));
	return reg;
}

static __inline uint32_t
mmu_probe(uint32_t va)
{
	return load_alternate(va & ~0xFFF | 0x400, 3);
}

#endif /* !__ASSEMBLER__ */

#endif /* !ROS_INC_X86_H */
