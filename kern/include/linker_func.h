/* Copyright (c) 2014 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Linker functions.  These are functions of type void func(void) that are
 * automatically run during the kernel initialization.
 *
 * There are different levels; the functions of lower levels are run before
 * higher levels.  There is no guarantee of ordering of functions within a
 * level.
 *
 * To use, change your function definition to use the appropriate macro, e.g.
 *
 * 		void foo(void)
 * 		{
 * 			bar();
 * 		}
 *
 * becomes
 *
 * 		linker_func_3(foo)
 * 		{
 * 			bar();
 * 		}
 *
 * And foo() will run during the third level of functions.
 *
 * For now, all levels are run sequentially, and with interrupts enabled. */

#pragma once

#define __linkerfunc1  __attribute__((__section__(".linkerfunc1")))
#define __linkerfunc2  __attribute__((__section__(".linkerfunc2")))
#define __linkerfunc3  __attribute__((__section__(".linkerfunc3")))
#define __linkerfunc4  __attribute__((__section__(".linkerfunc4")))

typedef void (*linker_func_t)(void);

#define linker_func_1(x)                                                       \
	void (x)(void);                                                        \
	linker_func_t __linkerfunc1 __##x = (x);                               \
	void (x)(void)

#define linker_func_2(x)                                                       \
	void (x)(void);                                                        \
	linker_func_t __linkerfunc2 __##x = (x);                               \
	void (x)(void)

#define linker_func_3(x)                                                       \
	void (x)(void);                                                        \
	linker_func_t __linkerfunc3 __##x = (x);                               \
	void (x)(void)

#define linker_func_4(x)                                                       \
	void (x)(void);                                                        \
	linker_func_t __linkerfunc4 __##x = (x);                               \
	void (x)(void)

extern linker_func_t __linkerfunc1start[];
extern linker_func_t __linkerfunc1end[];
extern linker_func_t __linkerfunc2start[];
extern linker_func_t __linkerfunc2end[];
extern linker_func_t __linkerfunc3start[];
extern linker_func_t __linkerfunc3end[];
extern linker_func_t __linkerfunc4start[];
extern linker_func_t __linkerfunc4end[];
