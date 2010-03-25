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
  int core_id = __syscall_sysenter(SYS_getvcoreid,0,0,0,0,0,NULL);

  static int initialized = 0;
  if(!initialized)
  {
    initialized = 1;

    size_t sz= (sizeof(segdesc_t)*__procinfo.max_harts+PGSIZE-1)/PGSIZE*PGSIZE;
    
    intreg_t params[3] = { MAP_ANONYMOUS | MAP_FIXED, -1, 0 };
    __procdata.ldt  = (void*) __syscall_sysenter(SYS_mmap,
                                                 (void*)USTACKBOT - LDT_SIZE, 
                                                 sz, PROT_READ | PROT_WRITE, 
                                                 params, 0, NULL);

    // force kernel crossing
    __syscall_sysenter(SYS_getpid,0,0,0,0,0,NULL);
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
