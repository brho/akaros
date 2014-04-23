// INFERNO
#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>

int getfields(char *str, char **args, int max, int mflag, char *set)
{
	//Rune r;
	int r;
	int nr, intok, narg;

	if (max <= 0)
		return 0;

	narg = 0;
	args[narg] = str;
	if (!mflag)
		narg++;
	intok = 0;
	for (;; str += nr) {
		r = str[0];
		//nr = chartorune(&r, str);
		nr = 1;

		if (r == 0)
			break;
		//if(utfrune(set, r)) {
		if (strchr(set, r)) {
			if (narg >= max)
				break;
			*str = 0;
			intok = 0;
			args[narg] = str + nr;
			if (!mflag)
				narg++;
		} else {
			if (!intok && mflag)
				narg++;
			intok = 1;
		}
	}
	return narg;
}
