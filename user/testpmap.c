//#ifdef LAB >= 4

#include <inc/lib.h>

int sequence_length = 16;
int sequence[] = { 0,  1,   1,   2,   3, 
		   5,  8,  13,  21,  34, 
		  55, 89, 144, 233, 377, 610};

void
mark_page(int* pg, int i) {
  memset(pg, 0, PGSIZE);

  // now dump a non-random sequence into the page
  // offset by i
  int j;
  for (j = 0; j < sequence_length; j++)
    pg[j] = i + sequence[j];
}

int
test_page(int* pg, int i) {
  int j;
  for (j = 0; j < sequence_length; j++)
    if (pg[j] != i + sequence[j])
      return 1;

  return 0;
}

void
print_marked_page(int* pg) {
  int j;
  for (j = 0; j < (sequence_length-1); j++)
    cprintf("%d, ", pg[j]);

  cprintf("%d", pg[j]);
}

void
print_expected_mark(int i) {
  int j;
  for (j = 0; j < (sequence_length-1); j++)
    cprintf("%d, ", sequence[j]+i);
  
  cprintf("%d", sequence[j]+i);
}

int n, va, r, initva, maxpa, maxva, maxnum, failures;
int *page_id;

int
alloc_range(int initaddr, int maxpa, int startn) {
  n = startn;
  maxnum = maxpa / PGSIZE;
  initva = initaddr;
  maxva = initva + maxpa;
  
  cprintf ("[%08x] trying to alloc pages in range [%08x, %08x]\n", env->env_id, initva, maxva);

  // how many pages can I alloc? 
  // - limit to 256 M worth of pages
  for (va = initva; va < maxva; va += PGSIZE, n++) { 
    // alloc a page 
    if ((r = sys_mem_alloc(0, va, PTE_P | PTE_U | PTE_W | PTE_AVAIL)) < 0) { 
      //cprintf("\nsys_mem_alloc failed: %e", r);
      break;
    }

    //page_id = (int*)va;
    //*page_id = n;              // mark this page...
    //memset((int*)va, n, PGSIZE / sizeof(int));
    mark_page((int*)va, n);

    if ( (((va - initva) / PGSIZE) % 128) == 0) cprintf(".");
  }
  cprintf("\n");

  cprintf("[%08x] able to allocate [%d] pages of requested [%d] pages\n", env->env_id, n, maxnum);

  maxva = va;
  return n;
}

int
test_range(int startva, int endva, int startn) {
  int c;
  cprintf("[%08x] testing pages in [%08x, %08x] to see if they look okay\n", env->env_id, startva, endva);
  n = startn;
  failures = 0;  
  for (va = startva, c = 0; va < endva; va += PGSIZE, n++, c++) { 
    page_id = (int*)va;

    if (test_page((int*)va, n)) {
      cprintf("\n[%08x] unexpected value at [%08x]:\n  {", env->env_id, va);
      print_marked_page((int*)va);
      cprintf("} should be\n  {");
      print_expected_mark(n);
      cprintf("}");
      
      failures++;
    } else {
      Pte pte = vpt[VPN(va)];
      int perm = (PTE_U | PTE_P | PTE_W | PTE_AVAIL);

      if ((pte & perm) != perm) {
	cprintf("\n[%08x] unexpected PTE permissions [04x] for address [%08x]\n {", env->env_id, pte & perm, va);
	failures++;
      }

      //      cprintf("\n value at [%08x]: {", va);
      //print_marked_page((int*)va);
      //cprintf("} should be {");
      //print_expected_mark(n);
      //cprintf("}");
    }

    if ( (((va - startva) / PGSIZE) % 128) == 0) cprintf(".");
    //if ((va % PDMAP) == 0) cprintf(".");
  }
  cprintf("\n");

  cprintf("[%08x] tested %d pages: %d failed assertions.\n", env->env_id, c, failures);

  return failures;
}

void
unmap_range(int startva, int endva) {
  cprintf("[%08x] unmapping range [%08x, %08x].\n", env->env_id, startva, endva);
  int xva, z;
  for (z=0, xva = startva; xva < endva; xva += PGSIZE, z++) { 
    sys_mem_unmap(0, xva);
  }
  cprintf("[%08x] unmapped %d pages.\n", env->env_id, z);
}

int
duplicate_range(int startva, int dupeva, int nbytes) {
  cprintf("[%08x] duplicating range [%08x, %08x] at [%08x, %08x]\n", 
	 env->env_id, startva, startva+nbytes, dupeva, dupeva+nbytes);
  int xva, r, k;
  for (xva = 0, k = 0; xva < nbytes; xva += PGSIZE, k+=PGSIZE) { 
    if ((r = sys_mem_map(0, startva+xva, 0, dupeva+xva, PTE_P | PTE_U | PTE_W | PTE_USER)) < 0) {
      cprintf ("[%08x] duplicate_range FAILURE: %e\n", env->env_id, r);
      return r;
    }
  }

  return k;
}

// This tries to stress test the pmap code...
// Not the most intelligent testing strategy, 
// just hammer at it and see if it breaks.
void
umain(int argc, char **argv)
{  
  int max, max2, k, j, i, dupesize, dupen;

  for (i = 0; i < 2; i++) { // might as well do this multiple times to stress the system...
    cprintf("PMAPTEST[%08x] starting ROUND %d.\n", env->env_id, i);

    // Try to allocate as many pages as possible...
    k = alloc_range(UTEXT+PDMAP, (256 * 1024 * 1024), 0);       // alloc as many as possible
    max = maxva;                                                // save maximum memory size
    test_range(UTEXT+PDMAP, max, 0);                            // test if all are unique pages

    // If we've corrupted kernel memory, a yield might expose a problem.
    cprintf("PMAPTEST[%08x] yielding...\n", env->env_id);
    sys_yield();
    cprintf("PMAPTEST[%08x] back.\n", env->env_id);

    // Free a couple of pages for use by page tables and other envs...
    unmap_range(max-16 * PGSIZE, max);                           // free some pages so we have wiggle room, if extra
    max -= 16 * PGSIZE;                                          // pages are needed for page tables...
    
    // Unmap last 1024 pages and then try to reallocate them in the same place
    unmap_range(max - PDMAP, max);                              // unmap last 1024 pages
    j = alloc_range(max - PDMAP, PDMAP, 0);                     // try to realloc them (<1024 if other envs alloc'd)
    max2 = maxva; // max2 <= max && max2 >= (max - PDMAP)
    test_range(max - PDMAP, max2, 0);                           // test if new pages are unique
  
    // Create duplicate mappings of the last 1024
    dupesize = duplicate_range(max2-PDMAP, max+PDMAP, j*PGSIZE); // create duplicate mappings
    test_range(max2-PDMAP, max2-PDMAP+dupesize, 0);             // test lower mapping
    test_range(max+PDMAP, max + PDMAP + dupesize, 0);           // test upper mapping
    dupen = *((int*)(max+PDMAP));
  
    // Remove one of the duplicate mappings and then unmap and realloc the last 1024 pages
    unmap_range(max2-PDMAP, max2-PDMAP+dupesize);           // unmap lower mapping
    j = alloc_range(max2-PDMAP, PDMAP, 0);                  // try to alloc something, should be below 1024 (really? other envs?)
    unmap_range(max2-2*PDMAP, max2 - PDMAP);                // free 1024 pages
    j = alloc_range(max2-2*PDMAP, PDMAP, 0);                // alloc new pages for free'd 1024
    //max2 = maxva; // max2 <= old_max2 - PDMAP
    
    // Test ranges...
    test_range(UTEXT+PDMAP, max2-2*PDMAP, 0);              // test entire lower range of pages
    test_range(max2-2*PDMAP, maxva, 0);                    // test entire lower range of pages
    test_range(max+PDMAP, max + PDMAP + dupesize, dupen);  // test upper range

    // Free everything
    unmap_range(UTEXT+PDMAP, maxva);                        
    unmap_range(max+PDMAP, max+PDMAP+dupesize);    
    
    //unmap_range(UTEXT+PDMAP, max);
  }

}

//#endif




