#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>

static char isfrog[256] = {
	 /*NUL*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*BKS*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*DLE*/ 1, 1, 1, 1, 1, 1, 1, 1,
	 /*CAN*/ 1, 1, 1, 1, 1, 1, 1, 1,
	['/'] 1,
	[0x7f] 1,
};

/*
 * Check that the name
 *  a) is in valid memory.
 *  b) is shorter than 2^16 bytes
 *  c) contains no frogs.
 * The first byte is known to be addressable by the requester, so the
 * routine works for kernel and user memory both.
 * The parameter slashok flags whether a slash character is an error
 * or a valid character.
 *
 * The parameter dup flags whether the string should be copied
 * out of user space before being scanned the second time.
 * (Otherwise a malicious thread could remove the NUL, causing us
 * to access unchecked addresses.)
 * This was from Plan 9, but the Rune support is gone.
 * Also, we removed the 'user pointer' check, but might want it again later. 
 */
char *validname0(char *aname, int slashok, int dup, uintptr_t pc)
{
	char *ename, *name, *s;
	int c, n;

	name = aname;
	ename = memchr(name, 0, (1 << 16));

	if (ename == NULL || ename - name >= (1 << 16))
		error("name too long");

	s = NULL;
	if (dup) {
		n = ename - name;
		s = kzmalloc(n + 1, KMALLOC_WAIT);
		memmove(s, name, n);
		s[n] = 0;
		aname = s;
		name = s;
	}

	while (*name) {
		/* all characters above '~' are ok */
		c = *(uint8_t *) name;
		if (c >= Runeself) {
			error("No UTF-8 in Akaros");
		} else {
			if (isfrog[c])
				if (!slashok || c != '/') {
					kfree(s);
					error("Bad character in name");
				}
			name++;
		}
	}
	return s;
}

/*
 * Rather than strncpy, which zeros the rest of the buffer, kstrcpy
 * truncates if necessary, always zero terminates, does not zero fill,
 * and puts ... at the end of the string if it's too long.  Usually used to
 * save a string in up->genbuf;
 */
void kstrcpy(char *s, char *t, int ns)
{
	int nt;

	nt = strlen(t);
	if (nt + 1 <= ns) {
		memmove(s, t, nt + 1);
		return;
	}
	/* too long */
	if (ns < 4) {
		/* but very short! */
		strncpy(s, t, ns);
		return;
	}
	/* truncate with ... at character boundary (very rare case) */
	memmove(s, t, ns - 4);
	ns -= 4;
	s[ns] = '\0';
	memmove(s + ns, "...", 3);
}
