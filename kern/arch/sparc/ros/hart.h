#ifndef _ROS_ARCH_HART_H
#define _ROS_ARCH_HART_H

#define __RAMP__
double do_fdiv(double,double);
double do_fsqrt(double);
double do_recip(double);
double do_rsqrt(double);

static inline int
__hart_self()
{
	int id;
	asm volatile ("mov %%g6,%0" : "=r"(id));
	return id;
}

static inline void
__hart_relax()
{
	// TODO: relax
}

static inline int
__hart_swap(int* addr, int val)
{
	asm volatile ("swap [%2],%0" :"=r"(val) :"0"(val),"r"(addr) :"memory");
	return val;
}

#endif
