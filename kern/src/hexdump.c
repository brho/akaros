/*
 * Copyright 2013 Google Inc.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <kdebug.h>
#include <kmalloc.h>
#include <string.h>
#include <assert.h>
#include <smp.h>
#include <pmap.h>

static int isprint(int c)
{
	return (c >= 32 && c <= 126);
}

void hexdump(void *v, int length)
{
	int i;
	uint8_t *m = v;
	uintptr_t memory = (uintptr_t) v;
	int all_zero = 0;

	print_lock();
	for (i = 0; i < length; i += 16) {
		int j;

		all_zero++;
		for (j = 0; (j < 16) && (i + j < length); j++) {
			if (m[i + j] != 0) {
				all_zero = 0;
				break;
			}
		}

		if (all_zero < 2) {
			printk("%08lx:", memory + i);
			for (j = 0; j < 16; j++)
				printk(" %02x", m[i + j]);
			printk("  ");
			for (j = 0; j < 16; j++)
				printk("%c", isprint(m[i + j]) ? m[i + j] : '.');
			printk("\n");
		} else if (all_zero == 2) {
			printk("...\n");
		}
	}
	print_unlock();
}

/* easier in monitor */
void pahexdump(uintptr_t pa, int len)
{
	void *v = KADDR(pa);
	hexdump(v, len);
}

/* Print a string, with printables preserved, and \xxx where not possible. */
int printdump(char *buf, int numprint, int buflen, uint8_t *data)
{
	int ret = 0;
	int ix = 0;

	if (buflen < 1)
		return ret;
	buf[ret++] = '\'';
	/* we want 2 bytes left in the buf (which is ret < buflen - 1), one for the
	 * char, and one for the \' after the loop. */
	while (ix < numprint && ret < (buflen - 1)) {
		if (isprint(data[ix])) {
			buf[ret++] = data[ix];
		} else if (ret < buflen - 4) {
			/* guarantee there is room for a \xxx sequence */
			ret += snprintf(&buf[ret], buflen-ret, "\\%03o", data[ix]);
		} else {
			break;
		}
		ix++;
	}
	buf[ret++] = '\'';
	return ret;
}
