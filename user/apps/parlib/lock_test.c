#include <parlib.h>
#include <ros/mman.h>
#include <ros/resource.h>
#include <stdio.h>
#include <hart.h>

#include <pthread.h>

pthread_mutex_t lock;

// ghetto udelay, put in a lib somewhere and export the tsc freq
#include <arch/arch.h>
void udelay(uint64_t usec, uint64_t tsc_freq)
{
	uint64_t start, end, now;

	start = read_tsc();
    end = start + (tsc_freq * usec) / 1000000;
	if (end == 0) printf("This is terribly wrong \n");
	do {
        cpu_relax();
        now = read_tsc();
	} while (now < end || (now > start && end < start));
	return;
}

__thread int temp;

void startup(void *arg)
{
        uint32_t vcoreid = hart_self();
        temp = 0xcafebabe;
	while(1)
	{
        	pthread_mutex_lock(&lock);
		printf("Hello from hart_entry in vcore %d with temp addr %p and temp %p\n",
               		vcoreid, &temp, temp);
		pthread_mutex_unlock(&lock);
	}
}


int main(int argc, char** argv)
{
	uint32_t vcoreid;
	error_t retval;

        pthread_mutex_init(&lock, NULL);

	if ((vcoreid = hart_self())) {
		printf("Should never see me! (from vcore %d)\n", vcoreid);
	} else { // core 0
		temp = 0xdeadbeef;
		printf("Hello from vcore %d with temp addr = %p and temp = %p\n",
		       vcoreid, &temp, temp);
		printf("Multi-Goodbye, world, from PID: %d!\n", sys_getpid());

		extern void *hart_startup_arg;
		extern void (*hart_startup)();
		hart_startup_arg = NULL;
		hart_startup = startup;		
		retval = hart_request(6);

	}
	printf("Vcore %d Done!\n", vcoreid);
	while (1);
	return 0;
}
