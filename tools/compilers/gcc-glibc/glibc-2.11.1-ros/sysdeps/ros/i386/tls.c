#include <tls.h>
#include <sys/mman.h>
#include <ros/arch/hart.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>
#include <ros/procdata.h>
#include <arch/mmu.h>
#include <assert.h>

const char* tls_init_tp(void* thrdescr)
{
  int core_id = __hart_self();

  static int initialized = 0;
  if(!initialized)
  {
    assert(core_id == 0);
    initialized = 1;

    size_t sz= (sizeof(segdesc_t)*__procinfo.max_harts+PGSIZE-1)/PGSIZE*PGSIZE;
    __procdata.ldt = mmap((void*)USTACKBOT - LDT_SIZE, sz,
                          PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    // force kernel crossing
    ros_syscall(SYS_getpid,0,0,0,0,0);
    if(__procdata.ldt == MAP_FAILED)
      return "tls couldn't allocate memory\n";
  }

  // Build the segment
  segdesc_t tmp = SEG(STA_W, (uint32_t)thrdescr, (uint32_t)thrdescr + 4, 3);

  // Setup the correct LDT entry for this hart
  __procdata.ldt[core_id] = tmp;

  // Create the GS register.
  uint32_t gs = (core_id << 3) | 0x07;

  // Set the GS register.
  asm volatile("movl %0,%%gs" : : "r" (gs));

  return NULL;
}
