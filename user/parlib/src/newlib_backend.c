#include <hart.h>
#include <parlib.h>
#include <unistd.h>
#include <newlib_backend.h>

/* sbrk()
 * Increase program data space. 
 * As malloc and related functions depend on this, it is 
 * useful to have a working implementation. 
 * The following suffices for a standalone system; it exploits the 
 * symbol _end automatically defined by the GNU linker.
 */
void* sbrk(ptrdiff_t incr) 
{
	debug_in_out("SBRK\n");
	debug_in_out("\tincr: %d\n", incr);	

	extern char (SNT RO _end)[];
	static void* heap_end = NULL;

	static hart_lock_t sbrk_lock = HART_LOCK_INIT;
	hart_lock_lock(&sbrk_lock);
	if (heap_end == NULL)
		heap_end = (void*)_end;

	uint8_t* prev_heap_end; 
	prev_heap_end = heap_end;
	if (sys_brk(heap_end + incr) != heap_end + incr) {
		debug_in_out("\tsys_brk(%p+%d) failed\n",heap_end,incr);
		hart_lock_unlock(&sbrk_lock);
		errno = ENOMEM;
		return (void*CT(1))TC(-1);
	}
     
	heap_end += incr;
	hart_lock_unlock(&sbrk_lock);
	debug_in_out("\treturning: %u\n", prev_heap_end);
	return (void*) prev_heap_end;
}
