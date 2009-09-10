#include <lib.h>
#include <ros/mman.h>
#include <stdio.h>

// ghetto udelay, put in a lib somewhere and export the tsc freq
#include <arch/arch.h>
void udelay(uint64_t usec, uint64_t tsc_freq)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (tsc_freq * usec) / 1000000;
	if (end == 0) cprintf("This is terribly wrong \n");
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
	return;
}

#include <arch/atomic.h>

int main(int argc, char** argv)
{
	uint32_t vcoreid;
	if ((vcoreid = newcore())) {
		cprintf("Hello from vcore %d\n", vcoreid);
	} else {
		cprintf("Hello from else vcore 0\n");
		void* addr;
		cprintf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());
		addr = sys_mmap((void*SNT)USTACKTOP - 20*PGSIZE, 8*PGSIZE, 3, MAP_FIXED, 0, 0);
		cprintf("got addr = 0x%08x\n", addr);
		*(int*)addr = 0xdeadbeef;
		*(int*)(addr + 3*PGSIZE) = 0xcafebabe;
		// these should work
		cprintf("reading addr: 0x%08x\n", *(int*)addr);
		cprintf("reading addr+3pg: 0x%08x\n", *(int*)(addr + 3*PGSIZE));
		// this should fault
		//*(int*)(addr - 3*PGSIZE) = 0xdeadbeef;
	}
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 10; j++)
			sys_null();
		cprintf("Hello from vcore %d, iteration %d\n", vcoreid, i);
	}
	cprintf("Vcore %d Done!\n", vcoreid);
	while (1);
	udelay(5000000, 1995014570); // KVM's freq.  Whatever.
	return 0;
}
