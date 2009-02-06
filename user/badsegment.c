// program to cause a general protection exception

#include <inc/lib.h>

void
umain(void)
{
	// Try to load the kernel's TSS selector into the DS register.
        //asm volatile("movw $28,%ax; movw %ax,%ds");
  
        // DP: 0x28 == 40
        asm volatile("movw $40,%ax; movw %ax,%ds");
}

