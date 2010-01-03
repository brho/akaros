#include <parlib.h>
#include <errno.h>

intreg_t syscall(uint16_t _num, intreg_t _a1,
                intreg_t _a2, intreg_t _a3,
                intreg_t _a4, intreg_t _a5)
{
	register intreg_t num asm("g1") = _num;
	register intreg_t a1 asm("o0") = _a1, a2 asm("o1") = _a2;
	register intreg_t a3 asm("o2") = _a3, a4 asm("o3") = _a4;
	register intreg_t a5 asm("o4") = _a5;

	asm volatile("ta 8" : "=r"(a1),"=r"(a2)
	             : "r"(num),"0"(a1),"1"(a2),"r"(a3),"r"(a4),"r"(a5));

	// move a1, a2 into regular variables so they're volatile across
	// procedure calls (of which errno is one)
	intreg_t ret = a1, err = a2;
	if(err != 0)
		errno = err;

	return ret;
}
