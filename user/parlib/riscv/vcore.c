/* TODO: implement me */
void __attribute__((noreturn)) __kernel_vcore_entry(void)
{
	/* The kernel sets the TLS desc for us, based on whatever is in VCPD.
	 *
	 * x86 32-bit TLS is pretty jacked up, so the kernel doesn't set the TLS
	 * desc for us.  it's a little more expensive to do it here, esp for
	 * amd64.  Can remove this when/if we overhaul 32 bit TLS.
	 *
	 * AFAIK, riscv's TLS changes are really cheap, and they don't do it in
	 * the kernel (yet/ever), so they can set their TLS here too. */
	int id = __vcore_id_on_entry;
	#ifndef __x86_64__
	set_tls_desc(vcpd_of(id)->vcore_tls_desc);
	#endif
	/* Every time the vcore comes up, it must set that it is in vcore
	 * context.  uthreads may share the same TLS as their vcore (when
	 * uthreads do not have their own TLS), and if a uthread was preempted,
	 * __vcore_context == FALSE, and that will continue to be true the next
	 * time the vcore pops up. */
	__vcore_context = TRUE;
	vcore_entry();
	fprintf(stderr, "vcore_entry() should never return!\n");
	abort();
	__builtin_unreachable();
}
