// program to cause a breakpoint trap

// TODO: have arch specific user includes
#ifdef __i386__
static __inline void
breakpoint(void)
{
	__asm __volatile("int3");
}
#else
static __inline void
breakpoint(void)
{
	asm volatile ("ta 0x7f");
}
#endif

int main(int argc, char** argv)
{
	breakpoint();
	return 0;
}

