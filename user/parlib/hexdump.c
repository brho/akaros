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


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static int isprint(int c)
{
	return (c >= 32 && c <= 126);
}

void hexdump(FILE *f, void *v, int length)
{
	int i;
	unsigned char *m = v;
	unsigned long memory = (unsigned long) v;
	int all_zero = 0;

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
			fprintf(f,"%08lx:", memory + i);
			for (j = 0; j < 16; j++)
				fprintf(f," %02x", m[i + j]);
			fprintf(f,"  ");
			for (j = 0; j < 16; j++)
				fprintf(f,"%c", isprint(m[i + j]) ? m[i + j] : '.');
			fprintf(f,"\n");
		} else if (all_zero == 2) {
			fprintf(f,"...\n");
		}
	}
}

