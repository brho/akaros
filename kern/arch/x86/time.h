/* Copyright (c) 2009 The Regents of the University of California
 * David (Yu) Zhu <yuzhu@cs.berkeley.edu>
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * See LICENSE for details. */

#ifndef ROS_KERN_ARCH_TIME_H
#define ROS_KERN_ARCH_TIME_H

// PIT (Programmable Interval Timer)
#define	TIMER_REG_CNTR0	0	/* timer counter 0 port */
#define	TIMER_REG_CNTR1	1	/* timer counter 1 port */
#define	TIMER_REG_CNTR2	2	/* timer counter 2 port */
#define	TIMER_REG_MODE	3	/* timer mode port */
#define	TIMER_SEL0	0x00	/* select counter 0 */
#define	TIMER_SEL1	0x40	/* select counter 1 */
#define	TIMER_SEL2	0x80	/* select counter 2 */
#define	TIMER_INTTC	0x00	/* mode 0, intr on terminal cnt */
#define	TIMER_ONESHOT	0x02	/* mode 1, one shot */
#define	TIMER_RATEGEN	0x04	/* mode 2, rate generator */
#define	TIMER_SQWAVE	0x06	/* mode 3, square wave */
#define	TIMER_SWSTROBE	0x08	/* mode 4, s/w triggered strobe */
#define	TIMER_HWSTROBE	0x0a	/* mode 5, h/w triggered strobe */
#define	TIMER_LATCH	0x00	/* latch counter for reading */
#define	TIMER_LSB	0x10	/* r/w counter LSB */
#define	TIMER_MSB	0x20	/* r/w counter MSB */
#define	TIMER_16BIT	0x30	/* r/w counter 16 bits, LSB first */
#define	TIMER_BCD	0x01	/* count in BCD */

#define PIT_FREQ 					1193182

#define IO_TIMER1   0x40	/* 8253 Timer #1 */
#define TIMER_CNTR0 (IO_TIMER1 + TIMER_REG_CNTR0)
#define TIMER_CNTR1 (IO_TIMER1 + TIMER_REG_CNTR1)
#define TIMER_CNTR2 (IO_TIMER1 + TIMER_REG_CNTR2)
#define TIMER_MODE  (IO_TIMER1 + TIMER_REG_MODE)

typedef struct system_timing {
	uint64_t tsc_freq;
	uint64_t bus_freq;
	uint64_t timing_overhead;
	uint16_t pit_divisor;
	uint8_t pit_mode;
} system_timing_t;

extern system_timing_t system_timing;

// PIT related
void pit_set_timer(uint32_t freq, uint32_t mode);
void timer_init(void);
void udelay_pit(uint64_t usec);
// TODO: right now timer defaults to TSC
uint64_t gettimer(void);
uint64_t getfreq(void);

#endif /* ROS_KERN_ARCH_TIME_H */
