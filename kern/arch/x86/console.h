/* Copyright (c) 2012 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 Console (keyboard/serial/monitor) interfaces */

#pragma once

#include <ros/common.h>
#include <sys/queue.h>

/* Types of console devices */
#define CONS_KB_DEV		1
#define CONS_SER_DEV		2

struct cons_dev;
/* Interrupt-driven console input devices */
struct cons_dev {
	SLIST_ENTRY(cons_dev)		next;
	int				type;	/* e.g., CONS_KB_DEV */
	int				val;	/* e.g., COM1 */
	int				irq;	/* desired irq */
	char				*model;	/* descriptive string */
	int (*getc)(struct cons_dev *, uint8_t *);
};
SLIST_HEAD(cons_dev_slist, cons_dev);
extern struct cons_dev_slist cdev_list;

void cons_init(void);
/* Returns 0 on success, with the char in *data */
int cons_get_char(struct cons_dev *cdev, uint8_t *data);
/* Returns any available character, or 0 for none (legacy helper) */
int cons_get_any_char(void);
/* Writes c to the monitor and to all CONS_SER_DEV console devices */
void cons_putc(int c);

/* TODO: remove us (and serial IO) */
void serial_send_byte(uint8_t b);
int serial_read_byte();
