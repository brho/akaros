#include <ros/common.h>
#include <arch/arch.h>
#include <ros/timer.h>
#include <ros/syscall.h>

#include <lib.h>
#include <measure.h>
#include <syswrapper.h>
#include <atomic.h>

/* System-Global shared page */
#define shared_page UGDATA

/* Actual Things in the shared memory space:
 * - First is the barrier
 * - Second is some job info in here too.
 */

barrier_t*COUNT(1) bar = (barrier_t*COUNT(1))TC(shared_page);
volatile uint32_t*COUNT(1) job_to_run =
    (uint32_t*COUNT(1))TC(shared_page + sizeof(barrier_t));

// Experiment constants
uint32_t corecount = 0;
#define MAX_CACHELINE_WRITES 129

// added by asw
intreg_t syscall(uint16_t num, intreg_t a1,
                intreg_t a2, intreg_t a3,
                intreg_t a4, intreg_t a5);

#define syscall_sysenter syscall
#define syscall_trap syscall

/* Syscall Methods:
 * Pulling these in directly so we can measure them without other overheads. 
 */
/*static uint32_t
syscall_sysenter(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	uint32_t ret;
    asm volatile(
            "pushl %%ebp\n\t"
			"pushl %%esi\n\t"
            "movl %%esp, %%ebp\n\t"
            "leal after_sysenter, %%esi\n\t"
            "sysenter\n\t"
            "after_sysenter:\n\t"
			"popl %%esi\n\t"
            "popl %%ebp\n\t"
            :"=a" (ret)
            : "a" (num),
                "d" (a1),
                "c" (a2),
                "b" (a3),
                "D" (a4)
        : "cc", "memory", "%esp");
	return ret;
}

static uint32_t
syscall_trap(int num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	uint32_t ret;
	asm volatile("int %1\n"
		: "=a" (ret)
		: "i" (T_SYSCALL),
		  "a" (num),
		  "d" (a1),
		  "c" (a2),
		  "b" (a3),
		  "D" (a4),
		  "S" (a5)
		: "cc", "memory");

	return ret;
}*/

/* Experiment Rappers.  Like Snoop Dogg. */
void null_wrapper(uint32_t iters)
{
	for (int i = 0; i < iters; i++)
		null();
}

void null_async_wrapper(uint32_t iters)
{
	async_desc_t *desc;
	async_rsp_t rsp;

	for (int i = 0; i < iters; i++)
		null_async(&desc);
	for (int i = 0; i < iters; i++)
		waiton_async_call(desc, &rsp);
}

void sysenter_wrapper(uint32_t iters)
{
	for (int i = 0; i < iters; i++)
		syscall_sysenter(666, 0, 0, 0, 0, 0);
}

void systrap_wrapper(uint32_t iters)
{
	for (int i = 0; i < iters; i++)
		syscall_trap(666, 0, 0, 0, 0, 0);
}

void buster_sync_wrapper(uint32_t iters, uint32_t num_writes, uint32_t flags)
{
	for (int i = 0; i < iters; i++)
		cache_buster(num_writes, 0, flags);
}

void buster_async_wrapper(uint32_t iters, uint32_t num_writes, uint32_t flags)
{
	async_desc_t desc1;
	async_rsp_t rsp1;
	for (int i = 0; i < iters; i++){
	 	syscall_desc_t* sys_desc = get_sys_desc(&desc1);
		if (sys_desc != NULL)
	 		sys_cache_buster_async(sys_desc, num_writes, 0, flags);	
	}
	waiton_async_call(&desc1, &rsp1);
}

void buster_thruput_sync(uint32_t flags, char*NTS text)
{
	uint64_t thruput_ticks = 0;;	
	for (int i = 1; i < MAX_CACHELINE_WRITES; i= i*2) {
		// BEGIN barrier
		waiton_barrier(bar);
		thruput_ticks = start_timing();	
		// Sync Buster throughput
		buster_sync_wrapper(100, i, flags);
		waiton_barrier(bar);
		thruput_ticks = stop_timing(thruput_ticks);	
		cprintf("XME:BUSTER:%d:S:%s:%d:W:%llu\n", corecount, text, i, thruput_ticks);
	}
}

void buster_thruput_async(uint32_t flags, char*NTS text)
{
	uint64_t thruput_ticks = 0;;	
	for (int i = 1; i < MAX_CACHELINE_WRITES; i= i*2) {
		// BEGIN barrier
		waiton_barrier(bar);
		thruput_ticks = start_timing();	
		buster_async_wrapper(100, i, flags);
		waiton_barrier(bar);
		thruput_ticks = stop_timing(thruput_ticks);	
		cprintf("XME:BUSTER:%d:A:%s:%d:W:%llu\n", corecount, text, i, thruput_ticks);
	}
}

void buster_latency_sync(uint32_t flags, char*NTS text) 
{
	uint64_t tick;
	for (int i = 1; i < MAX_CACHELINE_WRITES; i=i*2) { // 1 - 128
		measure_func_runtime(buster_sync_wrapper, 100,  i , flags);
		tick = measure_func_runtime(buster_sync_wrapper, 1,  i , flags);
		cprintf("XME:BUSTER:%d:S:%s:LATENCY:%d:W:%llu\n", corecount, text,i, tick);
	}
}

void buster_latency_async(uint32_t flags, char*NTS text) 
{
	uint64_t tick;
	for (int i = 1; i < MAX_CACHELINE_WRITES; i=i*2) { // 1 - 128
		measure_func_runtime(buster_async_wrapper, 100,  i , flags);
		tick = measure_func_runtime(buster_async_wrapper, 1,  i , flags);
		cprintf("XME:BUSTER:%d:A:%s:LATENCY:%d:W:%llu\n", corecount, text,i, tick);
	}
}

/* Finally, begin the test */
int main(int argc, char** argv)
{
	uint32_t coreid = getcpuid();
	uint64_t ticks;
	
	#define MAX_ITERS 10

	switch(*job_to_run) {
		case 0:
			/* NULL SYSCALLS */
			if (coreid == 2)
				cprintf("Null Syscall Tests\n");
			// Sync Null Syscalls
			sys_cache_invalidate();
			ticks = measure_func_runtime(null_wrapper, 1);
			cprintf("XME:Null:S:1:1:C:%llu\n", ticks);
			for (int i = 1; i <= MAX_ITERS; i++) {
				ticks = measure_func_runtime(null_wrapper, i);
				cprintf("XME:Null:S:1:%d:W:%llu\n", i, ticks);
			}

			// Async Null Syscalls
			sys_cache_invalidate();
			ticks = measure_func_runtime(null_async_wrapper, 1);
			cprintf("XME:Null:A:1:1:C:%llu\n", ticks);
			for (int i = 1; i <= MAX_ITERS; i++) {
				ticks = measure_func_runtime(null_async_wrapper, i);
				cprintf("XME:Null:A:1:%d:W:%llu\n", i, ticks);
			}

			// raw sysenter
			sys_cache_invalidate();
			ticks = measure_func_runtime(sysenter_wrapper, 1);
			cprintf("XME:SYSE:S:1:1:C:%llu\n", ticks);
			for (int i = 1; i <= MAX_ITERS; i++) {
				ticks = measure_func_runtime(sysenter_wrapper, i);
				cprintf("XME:SYSE:S:1:%d:W:%llu\n", i, ticks);
			}

			// raw trap
			sys_cache_invalidate();
			ticks = measure_func_runtime(systrap_wrapper, 1);
			cprintf("XME:TRAP:S:1:1:C:%llu\n", ticks);
			for (int i = 1; i <= MAX_ITERS; i++) {
				ticks = measure_func_runtime(systrap_wrapper, i);
				cprintf("XME:TRAP:S:1:%d:W:%llu\n", i, ticks);
			}

			break;
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			corecount = (*job_to_run);
			if (coreid == 2)
				cprintf("Sync Buster Tests for job: %d\n", *job_to_run);
			waiton_barrier(bar);
			sys_cache_invalidate();
			// Sync Buster latency
			buster_latency_sync(BUSTER_SHARED, "NOLOCK:NOSTRIDE");
			buster_latency_sync(BUSTER_SHARED|BUSTER_LOCKED, "LOCKED:NOSTRIDE");
			buster_latency_sync(BUSTER_SHARED|BUSTER_LOCKED|BUSTER_STRIDED, "LOCKED:STRIDE");
			buster_latency_sync(BUSTER_SHARED|BUSTER_STRIDED, "NOLOCK:STRIDE");
			// Sync Buster thruput
			buster_thruput_sync(BUSTER_SHARED|BUSTER_LOCKED, "LOCKED:NOSTRIDE");
			buster_thruput_sync(BUSTER_SHARED|BUSTER_LOCKED|BUSTER_STRIDED, "LOCKED:STRIDE");
			buster_thruput_sync(BUSTER_SHARED, "UNLOCKED:NOSTRIDE");
			buster_thruput_sync(BUSTER_SHARED|BUSTER_STRIDED, "UNLOCKED:STRIDE");
			break;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
			corecount = (*job_to_run) - 8;
			if (coreid == 2)
				cprintf("Async Buster Tests for job: %d\n", *job_to_run);
			
			buster_latency_async(BUSTER_SHARED, "NOLOCK:NOSTRIDE");
			buster_latency_async(BUSTER_SHARED|BUSTER_LOCKED, "LOCKED:NOSTRIDE");
			buster_latency_async(BUSTER_SHARED|BUSTER_LOCKED|BUSTER_STRIDED, "LOCKED:STRIDE");
			buster_latency_async(BUSTER_SHARED|BUSTER_STRIDED, "NOLOCK:STRIDE");
			if (coreid == 2)
				cprintf("Async Buster Thruput Tests\n");
			buster_thruput_async(BUSTER_SHARED|BUSTER_LOCKED, "LOCKED:NOSTRIDE");
			buster_thruput_async(BUSTER_SHARED|BUSTER_LOCKED|BUSTER_STRIDED, "LOCKED:STRIDE");
			buster_thruput_async(BUSTER_SHARED, "UNLOCKED:NOSTRIDE");
			buster_thruput_async(BUSTER_SHARED|BUSTER_STRIDED, "UNLOCKED:STRIDE");
			break;
		default:
			if (coreid == 2)
				cprintf("Bug in Job Selection!!!\n");
	}
	waiton_barrier(bar);
	//cprintf("Env %x, on core %d, finishes\n", sys_getpid(), coreid);
	return 0;
	/* Options for Cache Buster:
	 * BUSTER_SHARED
	 * BUSTER_STRIDED
	 * BUSTER_LOCKED
	 * BUSTER_PRINT_TICKS
	 */

	/*
	async_desc_t *desc1, *desc2;
	async_rsp_t rsp1, rsp2;
	cache_buster_async(&desc1, 20, 5, BUSTER_LOCKED | BUSTER_SHARED);
	cache_buster_async(&desc2, 10, 0, BUSTER_STRIDED);
	waiton_async_call(desc1, &rsp1);
	waiton_async_call(desc2, &rsp2);
	cache_buster(10, 0, BUSTER_STRIDED | BUSTER_LOCKED);
	TAGGED_TIMING_BEGIN(tst);
	//measure_function(sys_null(), NUM_ITERATIONS, "sys_null");
	//measure_function(asm volatile("nop;"), NUM_ITERATIONS, "nop");
	//measure_function(cprintf("Reg Sync call  \n"), 10, "printf");
	//measure_function_async(cprintf_async(&desc, "Cross-Core call\n"), desc, 10,\
	//                       1, "Async Printf");
	// Note this won't actually do 100 inner runs (first parameter).  will stop
	// making calls beyond the ring size and won't wait on any extra calls.
	measure_async_call(null_async(&desc), desc, 100, 100, "Async Null");
	for (int i=0; i<100;i++){
		TAGGED_TIMING_BEGIN(umain);
		TAGGED_TIMING_END(umain);
	}
	POOL_FOR_EACH(&timer_pool, print_timer);
	// Spin to make sure we don't have any resources deallocated before done
	while(1);
	*/
}
