#include <stdio.h>
#include <unistd.h>
#include <arch/kbdreg.h>

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

void clrscrn(int leaverows) {
for(int i=0; i<(CRT_ROWS-leaverows-1); i++)
	write(STDOUT_FILENO, "\n", 1);
}
