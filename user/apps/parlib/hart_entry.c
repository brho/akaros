// Stub file. Paul, clean this up sir.

#include <stdio.h>

void (*hart_startup)(void*arg);
void *hart_startup_arg;


void hart_entry(void) {
	
	if (hart_startup != NULL)
		hart_startup(hart_startup_arg);
}
