#pragma once

/* Full CPU memory barrier */
#define mb() __sync_synchronize()
/* Compiler memory barrier */
#define cmb() ({ asm volatile("" ::: "memory"); })
/* Partial CPU memory barriers */
#define rmb() mb()
#define wmb() mb()
#define wrmb() mb()
#define rwmb() mb()

/* Forced barriers, used for string ops, SSE ops, dealing with hardware, or
 * other places where you avoid 'normal' x86 read/writes (like having an IPI
 * beat a write) */
#define mb_f() mb()
#define rmb_f() rmb()
#define wmb_f() wmb()
#define wrmb_f() wrmb()
#define rwmb_f() rwmb()

/* Bus memory barriers */
#warning "Implement bus memory barriers"
#define bus_wmb() mb()
#define bus_rmb() mb()
